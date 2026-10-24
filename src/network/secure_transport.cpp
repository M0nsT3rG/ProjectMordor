#include "network/secure_transport.hpp"

#include <ctime>

#include "network/dtls_session.hpp"

namespace mordor::net {

namespace {

// DTLS 1.2 record header (13) + AES-GCM explicit nonce (8) + tag (16) + our type byte (1), with margin.
constexpr std::size_t kSecureOverhead = 64;

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
        slots_.clear();
        by_secure_.clear();
        inner_->close();
    }
    bool is_open() const override { return inner_->is_open(); }
    Endpoint local_endpoint() const override { return inner_->local_endpoint(); }
    TransportCapabilities capabilities() const override {
        auto c = inner_->capabilities();
        c.reliable_ordered = false;     // DTLS records ride the inner Unreliable class only
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
        ++sstats_.pending_opened;
        by_secure_[s.secure.id] = key(*inner_peer);
        return s.secure;
    }

    SendResult send(PeerId peer, std::span<const std::uint8_t> bytes, Delivery d, std::uint64_t) override {
        if (!inner_->is_open()) return SendResult::Closed;
        if (d == Delivery::ReliableOrdered) {
            ++stats_.unsupported;
            return SendResult::Unsupported;  // explicit: never a silent downgrade
        }
        auto* s = established(peer);
        if (!s) {
            ++stats_.not_connected;
            return SendResult::NotConnected;
        }
        if (bytes.size() > capabilities().max_payload) {
            ++stats_.too_large;
            return SendResult::TooLarge;
        }
        if (!s->established->send(bytes)) {
            ++stats_.send_failed;
            return SendResult::Failed;
        }
        ++stats_.sent;
        return SendResult::Sent;
    }

    void poll(std::uint64_t now_us, std::vector<TransportEvent>& out) override {
        std::vector<TransportEvent> ev;
        inner_->poll(now_us, ev);
        const auto unix_s = cfg_.unix_clock();
        for (auto& e : ev) {
            switch (e.kind) {
                case TransportEvent::Kind::Message: route(e, now_us, unix_s); break;
                case TransportEvent::Kind::Disconnected: {
                    auto it = slots_.find(key(e.peer));
                    if (it == slots_.end()) break;
                    if (it->second.established) out.push_back(disconnected(it->second, e.reason));
                    erase(it);
                    break;
                }
                case TransportEvent::Kind::DroppedInbound: out.push_back(std::move(e)); break;  // informational
                case TransportEvent::Kind::Connected: break;  // transport-level only: we report after DTLS
            }
        }
        for (auto it = slots_.begin(); it != slots_.end();) {
            auto& s = it->second;
            if (s.established) s.established->service(now_us, unix_s);
            if (s.pending) {
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
                inner_->close_peer(s.inner);
                it = erase(it);
                continue;
            }
            if (s.established)
                for (auto& m : s.established->take_received())
                    out.push_back({TransportEvent::Kind::Message, s.secure, s.remote, Delivery::Unreliable, std::move(m), {}});
            if (!s.established && !s.pending) {
                if (!listen_) out.push_back(disconnected(s, "dtls_handshake_failed"));  // our own connect failed
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
        erase(it);
        inner_->close_peer(inner_peer);
    }
    void flush_all() override { inner_->flush_all(); }
    bool set_impairment(const ImpairmentConfig& c) override { return inner_->set_impairment(c); }
    TransportKind kind() const override { return inner_->kind(); }
    const TransportStats& stats() const override { return stats_; }
    const SecureStats& secure() const { return sstats_; }

private:
    struct Slot {
        PeerId inner, secure;
        Endpoint remote;
        std::unique_ptr<DtlsSession> established, pending;
    };
    using Key = std::uint64_t;
    static Key key(PeerId p) { return (static_cast<Key>(p.id) << 32) | p.generation; }

    DtlsConfig session_cfg(bool server) const {
        DtlsConfig d;
        d.server = server;
        d.own_key = cfg_.own_key;
        d.pinned_peers = cfg_.pinned_peers;
        d.invitation = cfg_.invitation;
        d.mtu = inner_->capabilities().max_payload;
        d.handshake_timeout_us = cfg_.pending_lifetime_us;
        d.binding_timeout_us = cfg_.binding_timeout_us;
        return d;
    }
    std::size_t pending_total() const {
        std::size_t n = 0;
        for (const auto& [k, s] : slots_) n += s.pending ? 1 : 0;
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
            const bool hello = dtls_is_client_hello(e.bytes);
            const bool has_session = it != slots_.end() && (it->second.established || it->second.pending);
            if (!has_session || (it->second.established && !it->second.pending && hello)) {
                if (!hello) {
                    ++sstats_.dropped_not_dtls;  // no plaintext path, ever
                    return;
                }
                if (pending_total() >= cfg_.max_pending_total) {
                    ++sstats_.pending_refused_cap;  // bounded pre-authentication state (FB-7-01)
                    return;
                }
                if (it == slots_.end()) {
                    it = slots_.emplace(key(e.peer), Slot{}).first;
                    it->second.inner = e.peer;
                    it->second.remote = e.remote;
                }
                it->second.pending = std::make_unique<DtlsSession>(*inner_, e.peer, e.remote, session_cfg(true));
                ++sstats_.pending_opened;
            }
        }
        auto& s = it->second;
        // Each session silently drops records it cannot decrypt, so both may see every datagram.
        if (s.established) s.established->on_datagram(e.bytes, now_us, unix_s);
        if (s.pending) s.pending->on_datagram(e.bytes, now_us, unix_s);
    }

    void promote(Slot& s, std::vector<TransportEvent>& out) {
        if (s.established) {  // retire the old association: its peer is gone or reconnected
            out.push_back(disconnected(s, "replaced_by_new_session"));
            by_secure_.erase(s.secure.id);
            ++sstats_.promotions_replacing;
            s.secure = PeerId{next_id_++, 0};
        } else if (listen_) {
            s.secure = PeerId{next_id_++, 0};
        }
        s.established = std::move(s.pending);
        by_secure_[s.secure.id] = key(s.inner);
        ++sstats_.authenticated;
        out.push_back({TransportEvent::Kind::Connected, s.secure, s.remote, Delivery::Unreliable, {}, "authenticated"});
    }

    std::unique_ptr<Transport> inner_;
    SecureTransportConfig cfg_;
    bool listen_ = false;
    std::map<Key, Slot> slots_;            // by inner PeerId
    std::map<std::uint32_t, Key> by_secure_;  // secure PeerId.id -> slot
    std::uint32_t next_id_ = 1;
    TransportStats stats_;
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
