#include "network/secure_transport.hpp"

#include <array>
#include <chrono>
#include <ctime>
#include <deque>
#include <thread>

#include "network/dtls_session.hpp"

namespace mordor::net {

namespace {

// DTLS 1.2 record header (13) + AES-GCM explicit nonce (8) + tag (16) + DtlsSession type byte (1) + our channel
// header (<= 5), with margin.
constexpr std::size_t kSecureOverhead = 64;

// Secure reliable-ordered channel over the authenticated DTLS session (review #8 FB-8-02). Frames inside DTLS
// application records: 0x01 unreliable | 0x02 reliable [seq u32 LE] | 0x03 ack [next expected seq u32 LE].
// Cumulative acks, per-message retransmission with back-off, in-order delivery, duplicate suppression, a bounded
// send window (Backpressure) and a bounded reorder window.
// shortcut: cumulative acks only (go-back-N-like retransmission after a loss: ~2.4 retransmits per message at 15%
// loss, EXP-021); add selective acks if event volume grows. Sequence numbers do not wrap (2^32 messages).
constexpr std::uint8_t kFrameUnreliable = 0x01, kFrameReliable = 0x02, kFrameAck = 0x03;
constexpr std::uint8_t kFrameKeepalive = 0x04;  // sent after idle_timeout_us / 3 without traffic (FB-8-04)
constexpr std::size_t kMaxUnacked = 256;           // send window: beyond this send() returns Backpressure
// At most this many messages are on the wire (transmitted, unacked) at once; the rest of the window waits. DTLS
// drops records that arrive more than 64 records late (OpenSSL's anti-replay window), so a whole-window burst
// under jitter livelocked: 12041 retransmits and nothing delivered (EXP-021, once simulated latency was real).
constexpr std::size_t kMaxInFlight = 32;
constexpr std::uint32_t kReorderWindow = 1024;     // accepted out-of-order distance
constexpr std::uint64_t kRtoInitialUs = 200'000, kRtoMaxUs = 1'000'000, kAckIntervalUs = 5'000;
constexpr int kCloseDrainMs = 500;                 // bounded wait for unacknowledged reliable data in close()

void put_u32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
std::uint32_t get_u32(std::span<const std::uint8_t> b) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= std::uint32_t{b[static_cast<std::size_t>(i)]} << (8 * i);
    return v;
}

class SecureTransport final : public Transport {
public:
    SecureTransport(std::unique_ptr<Transport> inner, SecureTransportConfig cfg) : inner_(std::move(inner)), cfg_(std::move(cfg)) {
        if (!cfg_.unix_clock) cfg_.unix_clock = [] { return static_cast<std::uint64_t>(std::time(nullptr)); };
    }
    ~SecureTransport() override { close(); }

    bool open(const Endpoint& bind, bool listen, std::string* error) override {
        listen_ = listen;
        return inner_->open(bind, listen, error);
    }
    void close() override {
        if (inner_->is_open()) {
            // Give accepted reliable data (e.g. a graceful Disconnect) a bounded chance to be acknowledged.
            const auto start = std::chrono::steady_clock::now();
            const auto base = now_us_;
            std::vector<TransportEvent> sink;
            while (any_unacked() && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(kCloseDrainMs)) {
                const auto el = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
                poll(base + static_cast<std::uint64_t>(el.count()), sink);
                sink.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            for (const auto& [k, s] : slots_)
                if (!s.unacked.empty()) ++stats_.closed_with_unacked_reliable;  // per connection, never silent
        }
        slots_.clear();
        by_secure_.clear();
        inner_->close();
    }
    bool is_open() const override { return inner_->is_open(); }
    Endpoint local_endpoint() const override { return inner_->local_endpoint(); }
    TransportCapabilities capabilities() const override {
        auto c = inner_->capabilities();
        c.reliable_ordered = true;      // our own secure channel over DTLS (FB-8-02); never the inner reliable class
        c.reports_disconnects = true;   // we report peer loss (rejected, replaced, inner disconnect)
        c.max_payload = c.max_payload > kSecureOverhead ? c.max_payload - kSecureOverhead : 0;
        c.exact_loss_accounting = false;
        return c;
    }

    std::optional<PeerId> connect(const Endpoint& remote) override {
        if (!inner_->is_open()) return std::nullopt;
        for (auto& [ip, s] : slots_)  // idempotent while that session lives
            if (s.remote == remote && (s.established || s.pending)) return s.secure;
        const auto inner_peer = inner_->connect(remote);
        if (!inner_peer) return std::nullopt;
        auto& s = slots_[key(*inner_peer)];
        s = Slot{};
        s.inner = *inner_peer;
        s.remote = remote;
        s.secure = PeerId{next_id_++, 0};
        s.pending = std::make_unique<DtlsSession>(*inner_, *inner_peer, remote, session_cfg(false));
        // FB-11-01: on a connection-oriented inner transport the handshake starts when it reports Connected. Earlier
        // ClientHellos were queued by GNS while its own DTLS timer backed off (EXP-024).
        s.inner_up = !inner_->capabilities().reports_disconnects;
        s.pending_since_us = 0;  // set on the first poll: connect() has no clock (a 0 start time made 88 h "handshakes")
        ++sstats_.pending_opened;
        by_secure_[s.secure.id] = key(*inner_peer);
        return s.secure;
    }

    SendResult send(PeerId peer, std::span<const std::uint8_t> bytes, Delivery d, std::uint64_t now_us) override {
        if (now_us != 0) now_us_ = now_us;  // forwarded to the inner backend (simulated latency schedules by it)
        if (!inner_->is_open()) return SendResult::Closed;
        auto* s = established(peer);
        if (!s) {
            ++stats_.not_connected;
            return SendResult::NotConnected;
        }
        if (bytes.size() > capabilities().max_payload) {
            ++stats_.too_large;
            return SendResult::TooLarge;
        }
        std::vector<std::uint8_t> frame;
        if (d == Delivery::ReliableOrdered) {
            if (s->unacked.size() >= kMaxUnacked) {
                ++stats_.backpressure;
                return SendResult::Backpressure;
            }
            frame.push_back(kFrameReliable);
            put_u32(frame, s->next_seq);
            frame.insert(frame.end(), bytes.begin(), bytes.end());
            if (s->unacked.size() >= kMaxInFlight) {  // accepted, queued: transmitted when the window moves
                s->unacked.push_back({s->next_seq++, std::move(frame), 0, kRtoInitialUs, false});
                ++stats_.sent;
                return SendResult::Sent;
            }
        } else {
            frame.push_back(kFrameUnreliable);
            frame.insert(frame.end(), bytes.begin(), bytes.end());
        }
        if (!s->established->send(frame, now_us_)) {
            ++stats_.send_failed;
            return SendResult::Failed;
        }
        s->last_tx_us = now_us_;
        if (d == Delivery::ReliableOrdered) s->unacked.push_back({s->next_seq++, std::move(frame), now_us_, kRtoInitialUs, true});
        ++stats_.sent;
        return SendResult::Sent;
    }

    void poll(std::uint64_t now_us, std::vector<TransportEvent>& out) override {
        now_us_ = now_us;
        std::vector<TransportEvent> ev;
        inner_->poll(now_us, ev);
        const auto unix_s = cfg_.unix_clock();
        for (auto& e : ev) {
            switch (e.kind) {
                case TransportEvent::Kind::Message: route(e, now_us, unix_s); break;
                case TransportEvent::Kind::Disconnected: {
                    if (cfg_.trace) cfg_.trace("inner_disconnected", now_us, e.peer.id, e.reason);
                    auto it = slots_.find(key(e.peer));
                    if (it == slots_.end()) break;
                    if (!it->second.established && !listen_) {
                        // Our own connect failed below DTLS (e.g. the inner connection never came up): say so now
                        // instead of leaving the caller to its overall deadline.
                        ++sstats_.pending_failed;
                        out.push_back(disconnected(it->second, "inner_connect_failed"));
                        erase(it);
                        break;
                    }
                    if (it->second.established) {
                        // Data that arrived before the loss is still delivered, in order, before Disconnected
                        // (found by secure_close_drain_is_bounded_and_counts_unacked on GNS, EXP-021).
                        for (auto& m : it->second.established->take_received()) on_frame(it->second, m, out);
                        out.push_back(disconnected(it->second, e.reason));
                    }
                    lose_unacked(it->second);
                    erase(it);
                    break;
                }
                case TransportEvent::Kind::DroppedInbound: out.push_back(std::move(e)); break;  // informational
                case TransportEvent::Kind::Connected: {  // transport-level only: we report after DTLS
                    if (cfg_.trace) cfg_.trace("inner_connected", now_us, e.peer.id, "");
                    if (auto it = slots_.find(key(e.peer)); it != slots_.end()) it->second.inner_up = true;
                    break;
                }
            }
        }
        for (auto it = slots_.begin(); it != slots_.end();) {
            auto& s = it->second;
            if (s.pending && s.inner_up && s.pending_since_us == 0) s.pending_since_us = now_us;
            if (s.established) s.established->service(now_us, unix_s);
            if (s.pending && s.inner_up) {
                s.pending->service(now_us, unix_s);
                if (s.pending->state() == DtlsState::Authenticated) promote(s, out);
                else if (s.pending->state() == DtlsState::Rejected) {
                    ++sstats_.pending_failed;
                    s.pending.reset();  // an established session (if any) is untouched
                }
            }
            if (s.established && s.established->state() == DtlsState::Rejected) {
                ++sstats_.rejected_established;
                out.push_back(disconnected(s, "dtls_rejected"));
                lose_unacked(s);
                inner_->close_peer(s.inner);
                it = erase(it);
                continue;
            }
            if (s.established) {
                for (auto& m : s.established->take_received()) on_frame(s, m, out);
                service_reliable(s, now_us);
            }
            // Codex review of the combined diff (P2): idle is judged AFTER this poll's authenticated records refreshed
            // last_rx_us. Checked first, a poll that came late (a stalled caller) dropped a peer whose datagrams were
            // already waiting, together with those datagrams.
            if (s.established && cfg_.idle_timeout_us && now_us - s.last_rx_us > cfg_.idle_timeout_us) {
                ++sstats_.idle_timeouts;
                out.push_back(disconnected(s, "idle_timeout"));
                lose_unacked(s);
                inner_->close_peer(s.inner);
                it = erase(it);
                continue;
            }
            if (!s.established && !s.pending) {
                if (!listen_) out.push_back(disconnected(s, "dtls_handshake_failed"));  // our own connect failed
                // Codex review of the combined diff (P2): close the inner peer too. Otherwise a GNS connection stays
                // open, the next connect() gets that same connection back with no new inner Connected, and its
                // handshake never starts; on UDP the inner peer would linger in the peer table.
                inner_->close_peer(s.inner);
                it = erase(it);
                continue;
            }
            ++it;
        }
    }

    void flush(std::uint64_t now_us) override { inner_->flush(now_us); }
    void close_peer(PeerId peer) override {
        auto k = by_secure_.find(peer.id);
        if (k == by_secure_.end()) return;
        auto it = slots_.find(k->second);
        if (it == slots_.end() || it->second.secure != peer) return;
        const auto inner_peer = it->second.inner;
        if (!it->second.unacked.empty()) ++stats_.closed_with_unacked_reliable;  // per connection, never silent
        erase(it);
        inner_->close_peer(inner_peer);
    }
    void flush_all() override { inner_->flush_all(); }
    bool set_impairment(const ImpairmentConfig& c) override { return inner_->set_impairment(c); }
    TransportKind kind() const override { return inner_->kind(); }
    const TransportStats& stats() const override {
        stats_.simulated_drops = inner_->stats().simulated_drops;  // the impairment lives in the inner backend
        return stats_;
    }
    const SecureStats& secure() const { return sstats_; }

private:
    struct Slot {
        PeerId inner, secure;
        Endpoint remote;
        std::unique_ptr<DtlsSession> established, pending;
        std::array<std::uint8_t, 32> pending_random{};
        std::uint64_t last_rx_us = 0;  // last AUTHENTICATED record from this peer (idle timeout, FB-8-04/FB-9-01)
        std::uint64_t last_tx_us = 0;  // last frame we sent (keepalive)
        std::uint64_t pending_since_us = 0;  // when the current pending session was opened (diagnostics)
        bool inner_up = true;  // false: our connect() waits for the inner transport's Connected (FB-11-01)
        std::uint64_t hello_window_start_us = 0;  // reserved ClientHello budget of an established peer (answer 5)
        std::uint32_t hello_window_attempts = 0;
        // Secure reliable channel state, per established session (FB-8-02).
        struct OutMsg {
            std::uint32_t seq;
            std::vector<std::uint8_t> frame;
            std::uint64_t sent_us, rto_us;
            bool transmitted;  // false: accepted and queued behind kMaxInFlight
        };
        std::deque<OutMsg> unacked;
        std::uint32_t next_seq = 1, next_expected = 1;
        std::map<std::uint32_t, std::vector<std::uint8_t>> out_of_order;
        bool ack_due = false;
        std::uint64_t last_ack_us = 0;
        void reset_channel() {
            unacked.clear();
            out_of_order.clear();
            next_seq = next_expected = 1;
            ack_due = false;
        }
    };
    using Key = std::uint64_t;
    static Key key(PeerId p) { return (static_cast<Key>(p.id) << 32) | p.generation; }

    DtlsConfig session_cfg(bool server) const {
        DtlsConfig d;
        d.server = server;
        d.own_key = cfg_.own_key;
        d.pinned_peers = cfg_.pinned_peers;
        d.invitation = cfg_.invitation;
        d.trace = cfg_.trace;
        d.mtu = inner_->capabilities().max_payload;
        d.handshake_timeout_us = cfg_.pending_lifetime_us;
        d.binding_timeout_us = cfg_.binding_timeout_us;
        return d;
    }
    std::size_t unreserved_pending() const {  // pending sessions of peers that have no established session
        std::size_t n = 0;
        for (const auto& [k, s] : slots_) n += s.pending && !s.established ? 1 : 0;
        return n;
    }
    Slot* established(PeerId secure) {
        auto k = by_secure_.find(secure.id);
        if (k == by_secure_.end()) return nullptr;
        auto it = slots_.find(k->second);
        if (it == slots_.end() || it->second.secure != secure || !it->second.established) return nullptr;
        return &it->second;
    }
    TransportEvent disconnected(const Slot& s, std::string_view why) const {
        return {TransportEvent::Kind::Disconnected, s.secure, s.remote, Delivery::Unreliable, {}, why};
    }
    std::map<Key, Slot>::iterator erase(std::map<Key, Slot>::iterator it) {
        by_secure_.erase(it->second.secure.id);
        return slots_.erase(it);
    }

    void route(const TransportEvent& e, std::uint64_t now_us, std::uint64_t unix_s) {
        auto it = slots_.find(key(e.peer));
        if (!listen_) {  // client: only peers we connected to
            if (it == slots_.end()) {
                ++sstats_.dropped_unknown_peer;
                return;
            }
        } else {
            const auto ch = dtls_parse_client_hello(e.bytes);  // complete epoch-0 ClientHello, or nullopt
            Slot* s = it != slots_.end() ? &it->second : nullptr;
            // A ClientHello that is not a retransmission for an existing pending handshake goes through the
            // STATELESS cookie gate first (KI-30, review #10 answer 5): no per-peer state until the sender proved it
            // receives at its address. Forgeries from spoofed addresses therefore never allocate anything.
            const bool retransmit_for_pending = ch && s && s->pending && s->pending_random == ch->random;
            if (ch && !retransmit_for_pending) {
                if (!listener_) listener_ = std::make_unique<DtlsSession>(*inner_, PeerId{}, Endpoint{}, session_cfg(true));
                // A ClientHello with a VALID cookie already proved return routability: it never spends the budget,
                // so forgeries cannot starve a real player's second ClientHello (found by the FB-8-01 test). The
                // check is one HMAC; everything else is rate-limited before any OpenSSL work.
                // FB-15-01: where the inner transport itself proved this address answers (GNS), our cookie adds a round
                // trip and no guarantee; skipped only when configured, and never on UDP.
                const bool skip_cookie = cfg_.skip_cookie_when_inner_proves_routability && inner_->capabilities().proves_return_routability;
                const bool proven = skip_cookie || (!ch->cookie.empty() && listener_->cookie_valid_for(e.peer, e.remote, ch->cookie, now_us));
                if (now_us - window_start_us_ >= 1'000'000) {
                    window_start_us_ = now_us;
                    window_attempts_ = window_proven_ = 0;
                }
                // Three budgets per second (answer 5, independent review): unproven ClientHellos from strangers
                // share one; an address with an established session has its own reserved one, so spoofed floods
                // cannot keep a known player from getting a cookie; cookie-proven ones (each starts a full server
                // flight) have a separate global one, and their cookies expire (30-60 s).
                bool over = false;
                if (proven) {
                    over = ++window_proven_ > cfg_.max_new_handshakes_per_s;
                } else if (s && s->established) {
                    if (now_us - s->hello_window_start_us >= 1'000'000) {
                        s->hello_window_start_us = now_us;
                        s->hello_window_attempts = 0;
                    }
                    over = ++s->hello_window_attempts > cfg_.max_reconnect_hellos_per_s;
                } else {
                    over = ++window_attempts_ > cfg_.max_new_handshakes_per_s;
                }
                if (over) {
                    ++sstats_.hello_rate_limited;  // bounds the CPU spent on HelloVerifyRequests and flights
                    return;
                }
                if (!skip_cookie && !listener_->listen_offer(e.bytes, e.peer, e.remote, now_us)) {
                    ++sstats_.hello_verify_sent;
                    return;
                }
                // Cookie verified: this ClientHello is from a return-routable sender.
                const bool reserved = s && s->established;  // a known player reconnects outside the global cap
                if (!(s && s->pending) && !reserved && unreserved_pending() >= cfg_.max_pending_total) {
                    ++sstats_.pending_refused_cap;  // bounded pre-authentication state (FB-7-01)
                    if (!skip_cookie) listener_.reset();  // its state is bound to this refused peer: start a fresh one
                    return;
                }
                if (!s) {
                    it = slots_.emplace(key(e.peer), Slot{}).first;
                    it->second.inner = e.peer;
                    it->second.remote = e.remote;
                    s = &it->second;
                }
                if (s->pending) ++sstats_.pending_replaced;  // a new, cookie-proven attempt supersedes an older one
                if (skip_cookie) {
                    auto dcfg = session_cfg(true);
                    dcfg.cookie_exchange = false;
                    s->pending = std::make_unique<DtlsSession>(*inner_, e.peer, e.remote, std::move(dcfg));
                    ++sstats_.cookie_skipped;
                } else {
                    s->pending = std::move(listener_);  // already holds this ClientHello (DTLSv1_listen)
                }
                s->pending_since_us = now_us;
                s->pending_random = ch->random;
                ++sstats_.pending_opened;
                if (skip_cookie) s->pending->on_datagram(e.bytes, now_us, unix_s);  // the ClientHello starts the handshake
                else s->pending->service(now_us, unix_s);                           // continue with the buffered one
                return;
            }
            // Anything else continues an existing association only; there is no plaintext path, ever. (The earlier
            // per-address replacement and known-player rate-limit exemption were removed: with the stateless gate
            // above an unverified ClientHello can no longer allocate or displace anything, review #10 answer 5.)
            if (!s || (!s->established && !s->pending)) {
                ++sstats_.dropped_not_dtls;
                return;
            }
        }
        auto& s = it->second;
        // No liveness here: this datagram is not authenticated yet (review #9 FB-9-01). on_frame() refreshes it.
        // Each session silently drops records it cannot decrypt, so both may see every datagram.
        if (s.established) s.established->on_datagram(e.bytes, now_us, unix_s);
        if (s.pending) s.pending->on_datagram(e.bytes, now_us, unix_s);
    }

    bool any_unacked() const {
        for (const auto& [k, s] : slots_)
            if (s.established && !s.unacked.empty()) return true;
        return false;
    }
    void lose_unacked(Slot& s) {  // accepted reliable messages that will now never be acknowledged: counted
        stats_.accepted_then_failed += s.unacked.size();
        s.reset_channel();
    }
    void send_frame(Slot& s, const std::vector<std::uint8_t>& frame) {
        if (!s.established->send(frame, now_us_)) ++stats_.send_failed;
        s.last_tx_us = now_us_;
    }
    void on_frame(Slot& s, const std::vector<std::uint8_t>& f, std::vector<TransportEvent>& out) {
        s.last_rx_us = now_us_;  // an authenticated, decrypted record of the established session: the peer is alive
        if (f.empty()) return;
        const std::span<const std::uint8_t> body(f.data() + 1, f.size() - 1);
        if (f[0] == kFrameUnreliable) {
            out.push_back({TransportEvent::Kind::Message, s.secure, s.remote, Delivery::Unreliable, {body.begin(), body.end()}, {}});
        } else if (f[0] == kFrameReliable && body.size() >= 4) {
            const std::uint32_t seq = get_u32(body);
            s.ack_due = true;
            if (seq < s.next_expected || seq - s.next_expected >= kReorderWindow) {
                ++sstats_.reliable_duplicates;  // already delivered (or absurdly far): re-ack, never deliver twice
                return;
            }
            s.out_of_order.emplace(seq, std::vector<std::uint8_t>(body.begin() + 4, body.end()));
            for (auto it = s.out_of_order.find(s.next_expected); it != s.out_of_order.end(); it = s.out_of_order.find(s.next_expected)) {
                out.push_back({TransportEvent::Kind::Message, s.secure, s.remote, Delivery::ReliableOrdered, std::move(it->second), {}});
                s.out_of_order.erase(it);
                ++s.next_expected;
            }
        } else if (f[0] == kFrameAck && body.size() >= 4) {
            const std::uint32_t next = get_u32(body);
            while (!s.unacked.empty() && s.unacked.front().seq < next) s.unacked.pop_front();
        }
    }
    void service_reliable(Slot& s, std::uint64_t now_us) {
        std::size_t i = 0;
        for (auto& m : s.unacked) {
            if (i++ >= kMaxInFlight) break;  // only the first kMaxInFlight are ever on the wire
            if (!m.transmitted) {
                send_frame(s, m.frame);
                m.transmitted = true;
                m.sent_us = now_us;
            } else if (now_us - m.sent_us >= m.rto_us) {
                send_frame(s, m.frame);
                m.sent_us = now_us;
                m.rto_us = std::min(m.rto_us * 2, kRtoMaxUs);
                ++sstats_.reliable_retransmits;
            }
        }
        // A live but quiet peer must not be mistaken for a vanished one: keep the association warm.
        if (cfg_.idle_timeout_us && now_us - s.last_tx_us >= cfg_.idle_timeout_us / 3) send_frame(s, {kFrameKeepalive});
        if (s.ack_due && (now_us - s.last_ack_us >= kAckIntervalUs || !s.out_of_order.empty())) {
            std::vector<std::uint8_t> ack{kFrameAck};
            put_u32(ack, s.next_expected);
            send_frame(s, ack);
            s.ack_due = false;
            s.last_ack_us = now_us;
        }
    }

    void account_handshake(const Slot& s) {
        ++sstats_.handshakes_done;
        sstats_.handshake_retransmits += s.pending->stats().timeouts_handled;
        const auto ms = (now_us_ - s.pending_since_us) / 1000;
        sstats_.handshake_ms_total += ms;
        sstats_.handshake_ms_max = std::max(sstats_.handshake_ms_max, ms);
    }

    void promote(Slot& s, std::vector<TransportEvent>& out) {
        account_handshake(s);
        if (s.established) {  // retire the old association: its peer is gone or reconnected
            out.push_back(disconnected(s, "replaced_by_new_session"));
            lose_unacked(s);
            by_secure_.erase(s.secure.id);
            ++sstats_.promotions_replacing;
            s.secure = PeerId{next_id_++, 0};
        } else if (listen_) {
            s.secure = PeerId{next_id_++, 0};
        }
        s.established = std::move(s.pending);
        s.reset_channel();
        s.last_rx_us = s.last_tx_us = now_us_;
        by_secure_[s.secure.id] = key(s.inner);
        ++sstats_.authenticated;
        out.push_back({TransportEvent::Kind::Connected, s.secure, s.remote, Delivery::Unreliable, {}, "authenticated"});
    }

    std::unique_ptr<Transport> inner_;
    SecureTransportConfig cfg_;
    bool listen_ = false;
    std::uint64_t now_us_ = 0;  // time of the current poll()
    std::uint64_t window_start_us_ = 0;  // 1 s window of new handshake attempts (rate limit)
    std::uint32_t window_attempts_ = 0, window_proven_ = 0;
    std::unique_ptr<DtlsSession> listener_;  // stateless cookie gate (KI-30)
    std::map<Key, Slot> slots_;            // by inner PeerId
    std::map<std::uint32_t, Key> by_secure_;  // secure PeerId.id -> slot
    std::uint32_t next_id_ = 1;
    mutable TransportStats stats_;
    SecureStats sstats_;
};

}  // namespace

std::unique_ptr<Transport> make_secure_transport(std::unique_ptr<Transport> inner, const SecureTransportConfig& cfg,
                                                 std::string* error) {
    if (!inner || !cfg.own_key || cfg.pinned_peers.empty()) {
        if (error) *error = "secure transport needs an inner transport, an own key and at least one pinned peer";
        return nullptr;
    }
    return std::make_unique<SecureTransport>(std::move(inner), cfg);
}

const SecureStats* secure_stats(const Transport& t) {
    const auto* s = dynamic_cast<const SecureTransport*>(&t);
    return s ? &s->secure() : nullptr;
}

}  // namespace mordor::net
