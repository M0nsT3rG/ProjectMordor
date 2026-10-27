// DTLS 1.2 + RFC 7250 over a REAL Transport::Unreliable link, on every backend in this build (review #6
// FB-6-01/FB-6-02). Real time, loopback; OpenSSL's own timers drive retransmission. Impairment: UDP per instance
// (both directions), GNS process-global (one holder).
#include <openssl/evp.h>

#include <chrono>
#include <ctime>
#include <memory>
#include <set>

#include "network/dtls_session.hpp"
#include "test_support.hpp"

using namespace mordor;
using namespace mordor::net;

namespace {

struct Key {
    EVP_PKEY* p = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
    Key() { if (!p) throw test::CheckFailure("keygen failed"); }
    ~Key() { EVP_PKEY_free(p); }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    std::vector<std::uint8_t> spki() const { return spki_der(p); }
};

std::uint64_t now_us() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::uint64_t now_unix() { return static_cast<std::uint64_t>(std::time(nullptr)); }

std::vector<TransportKind> backends() {
    std::vector<TransportKind> out;
    for (auto k : {TransportKind::Udp, TransportKind::Gns})
        if (transport_available(k)) out.push_back(k);
    return out;
}

Invitation invite(const char* id = "invite-0001") { return {id, kProtocolVersion, now_unix() + 3600}; }

// A host transport + session per incoming peer, and a client transport + session to the host.
struct Link {
    TransportKind kind;
    Key host_key, client_key;
    DtlsConfig host_cfg, client_cfg;
    std::unique_ptr<Transport> host_t, client_t;
    std::unique_ptr<DtlsSession> host_s, client_s;
    std::unique_ptr<DtlsSession> host_pending;  // a new handshake on an established association (UDP reconnect)
    int promotions = 0, pending_opened = 0, pending_rejected = 0;
    PeerId to_host{};
    std::uint64_t client_clock_offset_s = 0;  // to simulate a client whose wall clock is ahead
    int service_every_ms = 0;                 // > 0: service() only this often (late / irregular scheduling)
    std::uint64_t service_calls = 0;
    std::vector<std::vector<std::uint8_t>> client_rx;  // every datagram delivered to the client (replay tests)
    std::chrono::steady_clock::time_point last_service{};

    explicit Link(TransportKind k, ImpairmentConfig imp = {}) : kind(k) {
        host_cfg.server = true;
        host_cfg.own_key = host_key.p;
        host_cfg.pinned_peers = {client_key.spki()};
        host_cfg.invitation = invite();
        client_cfg.own_key = client_key.p;
        client_cfg.pinned_peers = {host_key.spki()};
        client_cfg.invitation = host_cfg.invitation;
        std::string err;
        // UDP: impair both sides independently (different seeds). GNS: one process-global holder.
        ImpairmentConfig host_imp = imp, client_imp = imp;
        client_imp.seed = imp.seed + 1;
        const bool per_instance = make_transport(k, {}, &err)->capabilities().per_instance_impairment;
        host_t = make_transport(k, host_imp, &err);
        client_t = make_transport(k, per_instance ? client_imp : ImpairmentConfig{}, &err);
        if (!host_t || !host_t->open(Endpoint::loopback(0), true, &err)) throw test::CheckFailure("host open: " + err);
        if (!client_t || !client_t->open(Endpoint::loopback(0), false, &err)) throw test::CheckFailure("client open: " + err);
    }
    ~Link() {
        host_s.reset();
        client_s.reset();
        if (host_t) host_t->set_impairment({});
        if (client_t) client_t->set_impairment({});
    }
    void start_client() {
        to_host = client_t->connect(Endpoint::loopback(host_t->local_endpoint().port)).value();
        client_s = std::make_unique<DtlsSession>(*client_t, to_host, Endpoint::loopback(host_t->local_endpoint().port), client_cfg);
    }
    template <class Pred>
    bool pump(Pred done, int max_ms) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(max_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            std::vector<TransportEvent> ev;
            host_t->poll(now_us(), ev);
            for (auto& e : ev) {
                if (e.kind != TransportEvent::Kind::Message) continue;
                if (!host_s || host_s->peer() != e.peer) {  // first datagram of a (new) connection: new session
                    host_s = std::make_unique<DtlsSession>(*host_t, e.peer, e.remote, host_cfg);
                    host_pending.reset();
                } else if (!host_pending && host_s->state() != DtlsState::Handshaking && dtls_is_client_hello(e.bytes)) {
                    host_pending = std::make_unique<DtlsSession>(*host_t, e.peer, e.remote, host_cfg);
                    ++pending_opened;
                }
                // Each session silently drops records it cannot decrypt, so both may see every datagram.
                host_s->on_datagram(e.bytes, now_us(), now_unix());
                if (host_pending) host_pending->on_datagram(e.bytes, now_us(), now_unix());
            }
            if (host_pending) {
                host_pending->service(now_us(), now_unix());
                if (host_pending->state() == DtlsState::Authenticated) {
                    host_s = std::move(host_pending);
                    ++promotions;
                } else if (host_pending->state() == DtlsState::Rejected) {
                    host_pending.reset();  // the established session is untouched
                    ++pending_rejected;
                }
            }
            ev.clear();
            client_t->poll(now_us(), ev);
            for (auto& e : ev)
                if (e.kind == TransportEvent::Kind::Message && client_s && e.peer == to_host) {
                    client_rx.push_back(e.bytes);
                    client_s->on_datagram(e.bytes, now_us(), now_unix() + client_clock_offset_s);
                }
            const auto now = std::chrono::steady_clock::now();
            if (service_every_ms == 0 || now - last_service >= std::chrono::milliseconds(service_every_ms)) {
                last_service = now;
                ++service_calls;
                if (client_s) client_s->service(now_us(), now_unix() + client_clock_offset_s);
                if (host_s) host_s->service(now_us(), now_unix());
            }
            host_t->flush(now_us());
            client_t->flush(now_us());
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return done();
    }
    // 100% loss on (UDP) both instances / (GNS) the process-global holder; false restores the clean network.
    void set_cut(bool on) {
        ImpairmentConfig c;
        c.loss = on ? 1.0 : 0.0;
        CHECK(host_t->set_impairment(c));
        if (client_t->capabilities().per_instance_impairment) CHECK(client_t->set_impairment(c));
    }
    std::uint64_t retransmits() const {
        return (host_s ? host_s->stats().timeouts_handled : 0) + (client_s ? client_s->stats().timeouts_handled : 0);
    }
    bool both(DtlsState s) const { return host_s && client_s && host_s->state() == s && client_s->state() == s; }
    bool settled() const {  // both sides reached a final state
        auto final = [](const std::unique_ptr<DtlsSession>& x) {
            return x && (x->state() == DtlsState::Authenticated || x->state() == DtlsState::Rejected);
        };
        return final(host_s) && final(client_s);
    }
    void report(const char* test) const {
        auto st = [](const std::unique_ptr<DtlsSession>& x) {
            return x ? std::string(to_string(x->state())) + (x->reason().empty() ? "" : " (" + x->reason().substr(0, 60) + ")") : "none";
        };
        std::printf("       %s on %s: host %s, client %s\n", test, to_string(kind), st(host_s).c_str(), st(client_s).c_str());
    }
};

ImpairmentConfig bad_network() {
    ImpairmentConfig c;
    c.loss = 0.25;
    c.duplicate = 0.2;
    c.latency_ms = 20;
    c.jitter_ms = 30;
    c.seed = 41;
    return c;
}

}  // namespace

TEST(dtls_over_transport_authenticates_with_pinned_keys_and_binding) {
    for (auto k : backends()) {
        Link l(k);
        l.start_client();
        const bool authed = l.pump([&] { return l.both(DtlsState::Authenticated); }, 10000);
        l.report("dtls_over_transport_authenticates_with_pinned_keys_and_binding");
        CHECK(authed);
        CHECK(l.host_s->peer_fingerprint() == spki_fingerprint(l.client_key.spki()));
        CHECK(l.client_s->peer_fingerprint() == spki_fingerprint(l.host_key.spki()));
        CHECK(l.host_s->confirmation() == l.client_s->confirmation());
        CHECK(l.host_s->stats().cookies_verified >= 1);  // HelloVerifyRequest exchange happened
        const std::vector<std::uint8_t> a{1, 2, 3}, b{4, 5};
        CHECK(l.client_s->send(a) == SendResult::Sent);
        CHECK(l.host_s->send(b) == SendResult::Sent);
        std::vector<std::vector<std::uint8_t>> at_host, at_client;
        CHECK(l.pump([&] {
            for (auto& m : l.host_s->take_received()) at_host.push_back(m);
            for (auto& m : l.client_s->take_received()) at_client.push_back(m);
            return !at_host.empty() && !at_client.empty();
        }, 3000));
        CHECK(at_host.size() == 1 && at_host[0] == a);
        CHECK(at_client.size() == 1 && at_client[0] == b);
    }
}

// OpenSSL's retransmission under loss, duplication, delay and reordering; replay protection over a real link.
TEST(dtls_over_transport_survives_loss_jitter_reorder_and_duplication) {
    for (auto k : backends()) {
        Link l(k, bad_network());
        l.start_client();
        const bool authed = l.pump([&] { return l.both(DtlsState::Authenticated); }, 15000);
        l.report("dtls_over_transport_survives_loss_jitter_reorder_and_duplication");
        CHECK(authed);
        const auto retransmits = l.host_s->stats().timeouts_handled + l.client_s->stats().timeouts_handled;
        std::printf("       %s: %llu retransmission timeouts handled\n", to_string(k), static_cast<unsigned long long>(retransmits));
        if (k == TransportKind::Udp) CHECK(retransmits >= 1);  // seeded loss: deterministic; GNS loss is random
        std::multiset<std::uint8_t> got;
        for (std::uint8_t i = 1; i <= 60; ++i) CHECK(l.client_s->send(std::vector<std::uint8_t>{i}) == SendResult::Sent);
        l.pump([&] {
            for (auto& m : l.host_s->take_received()) got.insert(m[0]);
            return false;
        }, 2000);
        for (auto v : got) CHECK(got.count(v) == 1 && v >= 1 && v <= 60);  // duplicated datagrams never delivered twice
        std::printf("       %s: %zu of 60 unreliable app records arrived, none twice\n", to_string(k), got.size());
        CHECK(!got.empty());
    }
}

TEST(dtls_over_transport_wrong_pinned_key_is_rejected) {
    for (auto k : backends()) {
        Link l(k);
        Key mitm;
        l.client_cfg.pinned_peers = {mitm.spki()};  // the client expects someone else
        l.start_client();
        CHECK(l.pump([&] { return l.client_s->state() == DtlsState::Rejected; }, 10000));
        l.pump([&] { return l.host_s && l.host_s->state() == DtlsState::Rejected; }, 2000);
        l.report("dtls_over_transport_wrong_pinned_key_is_rejected");
        CHECK(l.client_s->reason().rfind("handshake_failed", 0) == 0);
        CHECK(!l.host_s || l.host_s->state() != DtlsState::Authenticated);
    }
}

// FB-6-01: the binding is fail-closed. Each mismatched field is rejected; neither side ever authenticates.
TEST(dtls_binding_rejects_mismatched_invitation_id_version_and_expiry) {
    struct Case {
        const char* name;
        void (*mutate)(Invitation&);
        const char* reason;
    };
    const Case cases[] = {
        {"id", [](Invitation& i) { i.id = "invite-9999"; }, "binding_invitation_mismatch"},
        {"version", [](Invitation& i) { i.protocol_version = static_cast<std::uint16_t>(i.protocol_version + 1); }, "binding_version_mismatch"},
        {"expiry", [](Invitation& i) { i.expires_unix_s += 60; }, "binding_expiry_mismatch"},
    };
    for (auto k : backends())
        for (const auto& c : cases) {
            Link l(k);
            c.mutate(l.client_cfg.invitation);
            l.start_client();
            CHECK(l.pump([&] { return l.settled(); }, 10000));
            std::printf("       mismatched %s: ", c.name);
            l.report("binding");
            CHECK(l.both(DtlsState::Rejected));
            CHECK(l.host_s->reason() == c.reason || l.client_s->reason() == c.reason);
            CHECK(l.host_s->stats().app_in == 0 && l.client_s->stats().app_in == 0);
        }
}

// A peer that completes DTLS but never sends its confirmation: the other side drops everything it sends and
// gives up (binding_timeout); it never accepts application data.
TEST(dtls_binding_missing_confirmation_fails_closed) {
    for (auto k : backends()) {
        Link l(k);
        l.client_cfg.send_binding = false;
        l.host_cfg.binding_timeout_us = 1'500'000;
        l.start_client();
        CHECK(l.pump([&] { return l.client_s->state() == DtlsState::Authenticated; }, 10000));  // it saw OUR binding
        for (std::uint8_t i = 0; i < 5; ++i) CHECK(l.client_s->send(std::vector<std::uint8_t>{i}) == SendResult::Sent);
        CHECK(l.pump([&] { return l.host_s->state() == DtlsState::Rejected; }, 5000));
        l.report("dtls_binding_missing_confirmation_fails_closed");
        CHECK(l.host_s->reason() == "binding_timeout");
        CHECK(l.host_s->stats().app_in == 0);
        CHECK(l.host_s->stats().app_dropped_before_auth >= 1);
        CHECK(l.host_s->take_received().empty());
    }
}

TEST(dtls_binding_expired_invitation_is_refused) {
    for (auto k : backends()) {
        Link l(k);
        l.client_clock_offset_s = 7200;  // the client's clock says the invitation (1 h) is already expired
        l.start_client();
        CHECK(l.pump([&] { return l.client_s->state() == DtlsState::Rejected; }, 3000));
        l.pump([] { return false; }, 300);
        l.report("dtls_binding_expired_invitation_is_refused");
        CHECK(l.client_s->reason() == "invitation_expired");
        CHECK(!l.host_s || l.host_s->state() != DtlsState::Authenticated);
    }
}

// Reconnect = a new transport connection and a completely new handshake; nothing carries over.
TEST(dtls_reconnect_is_a_new_handshake_with_a_fresh_binding) {
    for (auto k : backends()) {
        Link l(k);
        l.start_client();
        CHECK(l.pump([&] { return l.both(DtlsState::Authenticated); }, 10000));
        const auto first = l.client_s->confirmation();
        const auto* first_session = l.host_s.get();
        l.client_t->close_peer(l.to_host);
        l.client_s.reset();
        l.start_client();
        // UDP: same PeerId, pending session promoted. GNS: new connection, new PeerId, new session.
        CHECK(l.pump([&] { return l.both(DtlsState::Authenticated) && l.host_s.get() != first_session; }, 10000));
        std::printf("       %s: host promotions of a pending session: %d\n", to_string(k), l.promotions);
        l.report("dtls_reconnect_is_a_new_handshake_with_a_fresh_binding");
        CHECK(l.client_s->confirmation() != first);
        CHECK(l.client_s->confirmation() == l.host_s->confirmation());
    }
}

// A forged/replayed ClientHello on an established association must not disturb it: the pending session that it
// opens fails (no valid peer behind it) and the established one keeps working.
TEST(dtls_spurious_client_hello_does_not_disturb_an_established_session) {
    for (auto k : backends()) {
        Link l(k);
        if (l.client_t->capabilities().reports_disconnects) {
            // Connection-oriented: a third party cannot inject into the client's connection (GNS's own handshake
            // and encryption sit below us), so there is no association to spoof here.
            std::printf("       not applicable on %s: cannot inject into an existing connection\n", to_string(k));
            continue;
        }
        l.host_cfg.handshake_timeout_us = 1'500'000;
        l.start_client();
        CHECK(l.pump([&] { return l.both(DtlsState::Authenticated); }, 10000));
        const auto* established = l.host_s.get();
        // An epoch-0 ClientHello from the client's address with a key the host does not pin (what an off-path
        // attacker spoofing the address, or a replay, would produce).
        Key other;
        DtlsConfig fake = l.client_cfg;
        fake.own_key = other.p;
        {
            DtlsSession probe(*l.client_t, l.to_host, Endpoint::loopback(l.host_t->local_endpoint().port), fake);
            probe.service(now_us(), now_unix());  // emits one ClientHello through the client's socket, then goes away
        }
        CHECK(l.pump([&] { return l.pending_opened == 1 && l.host_pending == nullptr; }, 4000));
        CHECK(l.pending_rejected == 1 && l.promotions == 0);
        CHECK(l.host_s.get() == established && l.host_s->state() == DtlsState::Authenticated);
        CHECK(l.client_s->send(std::vector<std::uint8_t>{42}) == SendResult::Sent);
        std::vector<std::vector<std::uint8_t>> got;
        CHECK(l.pump([&] {
            for (auto& m : l.host_s->take_received()) got.push_back(m);
            return !got.empty();
        }, 3000));
        l.report("dtls_spurious_client_hello_does_not_disturb_an_established_session");
        CHECK(l.host_s.get() == established && got[0] == std::vector<std::uint8_t>{42});
    }
}

// ---- Review #7 FB-7-02: OpenSSL timer driving. The source of truth is DTLSv1_get_timeout: the remaining time of
// the current retransmission timer (doubling per retransmit). These tests check our scheduling around it. ----

TEST(dtls_timer_fires_with_late_irregular_service) {
    for (auto k : backends()) {
        Link l(k, bad_network());
        l.service_every_ms = 250;  // service() runs late: the expired timer must still fire on the next call
        l.start_client();
        const bool authed = l.pump([&] { return l.both(DtlsState::Authenticated); }, 20000);
        l.report("dtls_timer_fires_with_late_irregular_service");
        CHECK(authed);
        std::printf("       %s: %llu retransmits, %llu service calls\n", to_string(k), static_cast<unsigned long long>(l.retransmits()),
                    static_cast<unsigned long long>(l.service_calls));
        CHECK(l.retransmits() <= 20);
    }
}

// Independent review of #10: a cookie is bound to the peer, its address and a 30 s slot; it verifies for 30-60 s.
TEST(dtls_cookies_are_bound_to_peer_and_expire) {
    Link l(TransportKind::Udp);
    const PeerId peer{7, 1}, other{8, 1};
    const auto ep = Endpoint::loopback(1234);
    DtlsSession srv(*l.host_t, peer, ep, l.host_cfg);
    const std::uint64_t t0 = 90'000'000;
    CHECK(!srv.cookie_valid_for(peer, ep, {}, t0));
    unsigned char ck[64];
    unsigned int len = 0;
    CHECK(srv.cookie_generate(ck, &len));  // at t0 (set by the call above)
    const std::span<const std::uint8_t> cookie(ck, len);
    CHECK(srv.cookie_valid_for(peer, ep, cookie, t0 + 20'000'000));
    CHECK(srv.cookie_valid_for(peer, ep, cookie, t0 + 45'000'000));   // previous slot still accepted
    CHECK(!srv.cookie_valid_for(peer, ep, cookie, t0 + 65'000'000));  // expired
    CHECK(!srv.cookie_valid_for(other, ep, cookie, t0));
    CHECK(!srv.cookie_valid_for(peer, Endpoint::loopback(1235), cookie, t0));
}

// KI-35: a retransmitted flight must leave as ONE datagram (as its first transmission does), not one per record.
TEST(dtls_retransmitted_flight_is_one_datagram) {
    // UDP only: the window needs per-instance latency; GNS impairment is process-global (KI-24) and the cut did
    // not land inside it (0 rounds observed). The coalescing is in DtlsSession::flush_out, backend-independent.
    std::printf("       gns: NOT RUN (needs per-instance latency)\n");
    for (auto k : {TransportKind::Udp}) {
        Link l(k);
        ImpairmentConfig slow;  // 100 ms each way: the cut below lands before the client's flight reaches the host
        slow.latency_ms = 100;
        CHECK(l.host_t->set_impairment(slow));
        if (l.client_t->capabilities().per_instance_impairment) CHECK(l.client_t->set_impairment(slow));
        l.start_client();
        CHECK(l.pump([&] { return l.host_s && l.host_s->stats().datagrams_out >= 2; }, 5000));  // HVR + server flight
        l.set_cut(true);
        const auto rounds0 = l.host_s->stats().timeouts_handled, out0 = l.host_s->stats().datagrams_out;
        l.pump([] { return false; }, 1500);
        const auto rounds = l.host_s->stats().timeouts_handled - rounds0, out = l.host_s->stats().datagrams_out - out0;
        l.set_cut(false);
        std::printf("       %s: %llu retransmission rounds, %llu datagrams, %llu coalesced\n", to_string(k),
                    static_cast<unsigned long long>(rounds), static_cast<unsigned long long>(out),
                    static_cast<unsigned long long>(l.host_s->stats().datagrams_coalesced));
        CHECK(rounds >= 1);
        CHECK(out == rounds);  // was 5 datagrams per round (ServerHello, Certificate, KeyExchange, CertRequest, Done)
        CHECK(l.host_s->stats().datagrams_coalesced >= 4 * rounds);
        CHECK(l.pump([&] { return l.both(DtlsState::Authenticated); }, 15000));
    }
}

// FB-11-05: the same scenario with coalescing off reproduces OpenSSL's packetization (one datagram per record on
// retransmission), so the two can be compared; and a small MTU keeps every coalesced datagram within the limit,
// records never split across datagrams (each datagram parses as whole records).
TEST(dtls_coalescing_can_be_disabled_and_respects_the_mtu) {
    std::printf("       gns: NOT RUN (needs per-instance latency)\n");
    for (auto k : {TransportKind::Udp}) {
        Link l(k);
        l.host_cfg.coalesce_records = false;
        ImpairmentConfig slow;
        slow.latency_ms = 100;
        CHECK(l.host_t->set_impairment(slow));
        CHECK(l.client_t->set_impairment(slow));
        l.start_client();
        CHECK(l.pump([&] { return l.host_s && l.host_s->stats().datagrams_out >= 2; }, 5000));
        l.set_cut(true);
        const auto rounds0 = l.host_s->stats().timeouts_handled, out0 = l.host_s->stats().datagrams_out;
        l.pump([] { return false; }, 1500);
        const auto rounds = l.host_s->stats().timeouts_handled - rounds0, out = l.host_s->stats().datagrams_out - out0;
        l.set_cut(false);
        std::printf("       udp, coalescing off: %llu rounds, %llu datagrams\n", static_cast<unsigned long long>(rounds),
                    static_cast<unsigned long long>(out));
        CHECK(rounds >= 1 && out == 5 * rounds && l.host_s->stats().datagrams_coalesced == 0);
    }
    for (auto k : backends()) {
        Link l(k);
        l.host_cfg.mtu = l.client_cfg.mtu = 256;
        l.start_client();
        CHECK(l.pump([&] { return l.both(DtlsState::Authenticated); }, 10000));
        CHECK(l.host_s->stats().max_datagram_out <= 256 && l.client_s->stats().max_datagram_out <= 256);
        std::printf("       %s, MTU 256: coalesced host %llu, client %llu\n", to_string(k),
                    static_cast<unsigned long long>(l.host_s->stats().datagrams_coalesced),
                    static_cast<unsigned long long>(l.client_s->stats().datagrams_coalesced));
    }
}

// FB-11-05: malformed and truncated datagrams (bad lengths, cut records, garbage) are dropped by a handshaking
// server without harm, and the real handshake still completes afterwards.
TEST(dtls_malformed_and_truncated_datagrams_are_dropped) {
    for (auto k : backends()) {
        Link l(k);
        l.start_client();
        CHECK(l.pump([&] { return l.host_s != nullptr; }, 5000));
        const std::vector<std::vector<std::uint8_t>> junk = {
            {22, 254, 253, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 1},          // record length far beyond the datagram
            {22, 254, 253, 0, 0, 0, 0, 0, 0, 0, 1, 0, 4, 1, 0},               // handshake cut mid-header
            {23, 254, 253, 0, 1, 0, 0, 0, 0, 0, 9, 0, 3, 1, 2, 3},            // epoch-1 data before any keys
            {0, 1, 2, 3},                                                      // shorter than a record header
            std::vector<std::uint8_t>(1500, 0x16),                             // garbage, datagram-sized
        };
        for (const auto& j : junk) l.host_s->on_datagram(j, now_us(), now_unix());
        CHECK(l.host_s->state() != DtlsState::Authenticated);
        CHECK(l.pump([&] { return l.both(DtlsState::Authenticated); }, 15000));
        std::printf("       %s: %zu malformed datagrams dropped, handshake completed\n", to_string(k), junk.size());
    }
}

TEST(dtls_handshake_recovers_after_a_long_loss_window) {
    for (auto k : backends()) {
        Link l(k);
        l.set_cut(true);
        l.start_client();
        l.pump([] { return false; }, 3000);  // 3 s of total loss during the handshake
        const auto during = l.retransmits();
        l.set_cut(false);
        const bool authed = l.pump([&] { return l.both(DtlsState::Authenticated); }, 15000);
        l.report("dtls_handshake_recovers_after_a_long_loss_window");
        CHECK(authed);
        std::printf("       %s: %llu retransmits during the 3 s outage, %llu in total, %llu service calls\n", to_string(k),
                    static_cast<unsigned long long>(during), static_cast<unsigned long long>(l.retransmits()),
                    static_cast<unsigned long long>(l.service_calls));
        CHECK(during >= 1 && during <= 8);  // back-off 0.25, 0.5, 1, 2, 2 s ... per side: a handful, not one per poll
        CHECK(l.service_calls > 100 * during);  // no busy retransmission loop
    }
}

TEST(dtls_permanent_loss_ends_in_handshake_timeout_with_bounded_retransmits) {
    for (auto k : backends()) {
        Link l(k);
        l.client_cfg.handshake_timeout_us = 4'000'000;
        l.set_cut(true);
        l.start_client();
        CHECK(l.pump([&] { return l.client_s->state() == DtlsState::Rejected; }, 6000));
        l.report("dtls_permanent_loss_ends_in_handshake_timeout_with_bounded_retransmits");
        CHECK(l.client_s->reason() == "handshake_timeout");
        const auto r = l.client_s->stats().timeouts_handled;
        std::printf("       %s: %llu client retransmits in 4 s\n", to_string(k), static_cast<unsigned long long>(r));
        CHECK(r >= 3 && r <= 5);  // 0.25 + 0.5 + 1 + 2 s back-off: 4 attempts fit in 4 s (+1 for scheduling slack)
        l.set_cut(false);
    }
}

// ---- Review #7 FB-7-04: binding robustness. ----

TEST(dtls_binding_altered_confirmation_is_rejected) {
    for (auto k : backends()) {
        Link l(k);
        l.client_cfg.corrupt_confirmation = true;  // a peer whose confirmation does not match the exporter
        l.start_client();
        CHECK(l.pump([&] { return l.host_s && l.host_s->state() == DtlsState::Rejected; }, 10000));
        l.report("dtls_binding_altered_confirmation_is_rejected");
        CHECK(l.host_s->reason() == "binding_confirmation_mismatch");
        CHECK(l.host_s->stats().app_in == 0);
    }
}

TEST(dtls_binding_survives_duplicated_reordered_bindings_with_bounded_replies) {
    for (auto k : backends()) {
        ImpairmentConfig messy = bad_network();
        messy.loss = 0.2;
        messy.duplicate = 0.5;
        messy.jitter_ms = 60;
        Link l(k, messy);
        l.start_client();
        const bool authed = l.pump([&] { return l.both(DtlsState::Authenticated); }, 20000);
        l.pump([] { return false; }, 1500);  // keep running: no reply storm once both are done
        l.report("dtls_binding_survives_duplicated_reordered_bindings_with_bounded_replies");
        CHECK(authed);
        const auto& h = l.host_s->stats();
        const auto& c = l.client_s->stats();
        std::printf("       %s: bindings sent host %llu / client %llu, replies suppressed %llu / %llu\n", to_string(k),
                    static_cast<unsigned long long>(h.binding_sent), static_cast<unsigned long long>(c.binding_sent),
                    static_cast<unsigned long long>(h.binding_replies_suppressed), static_cast<unsigned long long>(c.binding_replies_suppressed));
        CHECK(h.binding_sent <= 30 && c.binding_sent <= 30);  // <= binding_timeout / resend + a few replies
    }
}

TEST(dtls_binding_invitation_expiring_during_a_long_handshake_is_refused) {
    for (auto k : backends()) {
        Link l(k);
        l.host_cfg.invitation.expires_unix_s = now_unix() + 2;  // valid when the handshake starts...
        l.client_cfg.invitation = l.host_cfg.invitation;
        l.set_cut(true);
        l.start_client();
        l.pump([] { return false; }, 3500);  // ...expired by the time the network lets it finish
        l.set_cut(false);
        CHECK(l.pump([&] { return l.settled(); }, 15000));
        l.report("dtls_binding_invitation_expiring_during_a_long_handshake_is_refused");
        CHECK(l.both(DtlsState::Rejected));
        CHECK(l.host_s->reason() == "invitation_expired" || l.client_s->reason() == "invitation_expired");
    }
}

TEST(dtls_small_mtu_fragments_the_handshake_within_the_limit) {
    for (auto k : backends()) {
        Link l(k);
        l.host_cfg.mtu = l.client_cfg.mtu = 256;
        l.start_client();
        const bool authed = l.pump([&] { return l.both(DtlsState::Authenticated); }, 10000);
        l.report("dtls_small_mtu_fragments_the_handshake_within_the_limit");
        CHECK(authed);
        std::printf("       %s: largest datagram host %zu, client %zu bytes\n", to_string(k), l.host_s->stats().max_datagram_out,
                    l.client_s->stats().max_datagram_out);
        CHECK(l.host_s->stats().max_datagram_out <= 256 && l.client_s->stats().max_datagram_out <= 256);
    }
}

// EXP-043 / FB-28-02: confirm_binding (an experiment, default off; both ends). The host acks the client's verified
// binding, which is when it starts accepting application data; the client becomes Authenticated only once it has
// verified the host's binding AND received that ack. The host never waits.
namespace {
void confirm_both(Link& l, std::uint64_t timeout_us = 2'000'000) {
    l.host_cfg.confirm_binding = l.client_cfg.confirm_binding = true;
    l.host_cfg.confirm_timeout_us = l.client_cfg.confirm_timeout_us = timeout_us;
}
}  // namespace

// The point of the experiment: the client is never Authenticated (so never sends application data) before the host
// has verified the client's binding. Without the flag the client authenticates first (the host's binding arrives in
// the host's last flight), which is what let EXP-042's first request reach a host that still dropped it.
TEST(dtls_confirm_binding_client_waits_until_the_host_verified_it) {
    for (auto k : backends()) {
        ImpairmentConfig delay;  // ordered and lossless: only the ordering of the two Authenticated states matters
        delay.latency_ms = 40;
        Link l(k, delay);
        confirm_both(l);
        l.start_client();
        int client_first = 0;
        const bool authed = l.pump([&] {
            if (l.client_s && l.client_s->state() == DtlsState::Authenticated &&
                !(l.host_s && l.host_s->state() == DtlsState::Authenticated))
                ++client_first;
            return l.both(DtlsState::Authenticated);
        }, 10000);
        l.report("dtls_confirm_binding_client_waits_until_the_host_verified_it");
        CHECK(authed);
        CHECK(client_first == 0);
        const auto& h = l.host_s->stats();
        const auto& c = l.client_s->stats();
        CHECK(h.binding_acks_sent >= 1 && c.binding_acks_received >= 1);
        CHECK(c.binding_acks_sent == 0 && h.binding_acks_received == 0);  // the host never waits, the client never acks
        CHECK(h.gate_timeouts == 0 && c.gate_timeouts == 0);
        // The first application record the client sends now always finds an authenticated host.
        CHECK(l.client_s->send(std::vector<std::uint8_t>{7}) == SendResult::Sent);
        std::vector<std::vector<std::uint8_t>> got;
        CHECK(l.pump([&] {
            for (auto& m : l.host_s->take_received()) got.push_back(m);
            return !got.empty();
        }, 3000));
        CHECK(h.app_dropped_before_auth == 0);
    }
}

// An ack proves only that the host verified OUR binding. It never authenticates the host: a host whose own binding
// never arrives (send_binding = false), but that acks the client's, leaves the client unauthenticated until
// binding_timeout, fail-closed. The client cannot send meanwhile.
TEST(dtls_confirm_binding_ack_never_authenticates_without_a_verified_binding) {
    for (auto k : backends()) {
        Link l(k);
        confirm_both(l);
        l.host_cfg.send_binding = false;  // acks the client's binding, never sends its own
        l.client_cfg.binding_timeout_us = 1'500'000;
        l.start_client();
        bool ever_authenticated = false;
        CHECK(l.pump([&] {
            ever_authenticated |= l.client_s && l.client_s->state() == DtlsState::Authenticated;
            return l.client_s && l.client_s->state() == DtlsState::Rejected;
        }, 10000));
        l.report("dtls_confirm_binding_ack_never_authenticates_without_a_verified_binding");
        CHECK(!ever_authenticated);
        CHECK(l.client_s->reason() == "binding_timeout");
        CHECK(l.client_s->stats().binding_acks_received >= 1);  // the ack did arrive: it was not enough
        CHECK(l.client_s->send(std::vector<std::uint8_t>{1}) == SendResult::Failed);
    }
}

// An altered binding is rejected as before, with the flag on.
TEST(dtls_confirm_binding_keeps_rejecting_an_altered_binding) {
    for (auto k : backends()) {
        Link l(k);
        confirm_both(l);
        l.host_cfg.corrupt_confirmation = true;
        l.start_client();
        CHECK(l.pump([&] { return l.client_s && l.client_s->state() == DtlsState::Rejected; }, 10000));
        l.report("dtls_confirm_binding_keeps_rejecting_an_altered_binding");
        CHECK(l.client_s->reason() == "binding_confirmation_mismatch");
        CHECK(l.client_s->stats().app_in == 0);
    }
}

// Premature records with the flag on: a client whose binding never reaches the host gets no ack, falls back after
// confirm_timeout (gate_timeouts) and sends; the host drops every record, delivers none later, and fails closed.
TEST(dtls_confirm_binding_premature_records_are_dropped_and_never_retained) {
    for (auto k : backends()) {
        Link l(k);
        confirm_both(l, 300'000);
        l.client_cfg.send_binding = false;
        l.host_cfg.binding_timeout_us = 1'500'000;
        l.start_client();
        CHECK(l.pump([&] { return l.client_s->state() == DtlsState::Authenticated; }, 10000));
        CHECK(l.client_s->stats().gate_timeouts == 1);
        for (std::uint8_t i = 0; i < 5; ++i) CHECK(l.client_s->send(std::vector<std::uint8_t>{i}) == SendResult::Sent);
        CHECK(l.pump([&] { return l.host_s->state() == DtlsState::Rejected; }, 5000));
        l.report("dtls_confirm_binding_premature_records_are_dropped_and_never_retained");
        CHECK(l.host_s->reason() == "binding_timeout");
        CHECK(l.host_s->stats().app_in == 0);
        CHECK(l.host_s->stats().app_dropped_before_auth >= 1);
        CHECK(l.host_s->take_received().empty());
    }
}

// A flag mismatch never hangs. A gated client with a host that never acks proceeds after confirm_timeout
// (gate_timeouts), exactly as without the flag; a host with the flag and a client without it changes nothing.
TEST(dtls_confirm_binding_mismatch_falls_back_without_hanging) {
    for (auto k : backends()) {
        for (const bool client_side : {true, false}) {
            Link l(k);
            (client_side ? l.client_cfg : l.host_cfg).confirm_binding = true;
            (client_side ? l.client_cfg : l.host_cfg).confirm_timeout_us = 500'000;
            l.start_client();
            const bool authed = l.pump([&] { return l.both(DtlsState::Authenticated); }, 10000);
            l.report(client_side ? "mismatch (client gated)" : "mismatch (host acks, client ignores)");
            CHECK(authed);
            CHECK(l.client_s->stats().gate_timeouts == (client_side ? 1u : 0u));
            CHECK(l.host_s->stats().gate_timeouts == 0);
            if (!client_side) CHECK(l.client_s->stats().binding_acks_received == 0);  // ignored, unknown to it
        }
    }
}

// Loss, duplication, delay and reordering: both sides still authenticate (the client resends its binding while it
// waits; every host reply repeats the ack), replayed acks change nothing, and the client's first record is not
// dropped by the host unless the client's gate timed out.
TEST(dtls_confirm_binding_survives_loss_jitter_reorder_and_duplication) {
    for (auto k : backends()) {
        Link l(k, bad_network());
        confirm_both(l);
        l.start_client();
        bool sent = false;
        const bool authed = l.pump([&] {
            if (!sent && l.client_s && l.client_s->state() == DtlsState::Authenticated)
                sent = l.client_s->send(std::vector<std::uint8_t>{9}) == SendResult::Sent;
            return l.both(DtlsState::Authenticated);
        }, 20000);
        l.pump([] { return false; }, 1000);
        l.report("dtls_confirm_binding_survives_loss_jitter_reorder_and_duplication");
        CHECK(authed);
        const auto& h = l.host_s->stats();
        const auto& c = l.client_s->stats();
        std::printf("       %s: acks sent %llu, received %llu, client gate timeouts %llu, host pre-auth drops %llu\n",
                    to_string(k), static_cast<unsigned long long>(h.binding_acks_sent),
                    static_cast<unsigned long long>(c.binding_acks_received), static_cast<unsigned long long>(c.gate_timeouts),
                    static_cast<unsigned long long>(h.app_dropped_before_auth));
        if (c.gate_timeouts == 0) CHECK(h.app_dropped_before_auth == 0);
        CHECK(h.binding_sent <= 30 && c.binding_sent <= 30);  // bounded, as without the flag
    }
}

// FB-29-03: BindingAck lifecycle. A delayed, duplicated, replayed or cross-session ack never authenticates an
// unverified peer, never crosses sessions and never causes a second transition. Both backends.

// A delayed ack: the client's gate times out first (it proceeds as without the flag), then the ack arrives and
// changes nothing.
TEST(dtls_confirm_binding_late_ack_after_the_gate_changes_nothing) {
    for (auto k : backends()) {
        ImpairmentConfig slow;
        slow.latency_ms = 150;  // the ack needs a round trip after the client verified the host's binding
        Link l(k, slow);
        confirm_both(l, 100'000);  // shorter than that round trip: the gate expires first
        l.start_client();
        CHECK(l.pump([&] { return l.both(DtlsState::Authenticated); }, 10000));
        CHECK(l.pump([&] { return l.client_s->stats().binding_acks_received >= 1; }, 3000));  // the late ack arrived
        l.report("dtls_confirm_binding_late_ack_after_the_gate_changes_nothing");
        CHECK(l.client_s->stats().gate_timeouts == 1);
        CHECK(l.client_s->state() == DtlsState::Authenticated);
        CHECK(l.client_s->reason().empty());
    }
}

// Duplicated datagrams and replayed acks: DTLS drops a record it already read, so an ack is counted at most once
// per ack the host actually sent, and replaying everything the client ever received changes no state and delivers
// nothing twice.
TEST(dtls_confirm_binding_duplicated_and_replayed_acks_are_inert) {
    for (auto k : backends()) {
        ImpairmentConfig dup;
        dup.duplicate = 0.9;
        dup.seed = 7;
        Link l(k, dup);
        confirm_both(l);
        l.start_client();
        CHECK(l.pump([&] { return l.both(DtlsState::Authenticated); }, 10000));
        CHECK(l.host_s->send(std::vector<std::uint8_t>{5}) == SendResult::Sent);
        std::size_t got = 0;
        l.pump([&] { got += l.client_s->take_received().size(); return false; }, 500);
        const auto acks = l.client_s->stats().binding_acks_received;
        const auto app_in = l.client_s->stats().app_in;
        l.report("dtls_confirm_binding_duplicated_and_replayed_acks_are_inert");
        CHECK(acks >= 1 && acks <= l.host_s->stats().binding_acks_sent);  // duplicates never counted twice
        CHECK(got == 1);
        const auto captured = l.client_rx;  // replay every datagram the client received, ack included
        for (const auto& d : captured) l.client_s->on_datagram(d, now_us(), now_unix());
        CHECK(l.client_s->state() == DtlsState::Authenticated);
        CHECK(l.client_s->stats().binding_acks_received == acks);
        CHECK(l.client_s->stats().app_in == app_in);
        CHECK(l.client_s->take_received().empty());
    }
}

namespace {
bool epoch0(const std::vector<std::uint8_t>& d) { return d.size() > 4 && d[3] == 0 && d[4] == 0; }  // plaintext
}  // namespace

// Reconnect boundary: the previous session's encrypted records (its binding and ack) replayed into a new client
// session cannot authenticate it nor count as its ack (different keys); the new session then authenticates on its
// own host's binding and ack, without a gate timeout.
TEST(dtls_confirm_binding_an_old_sessions_ack_never_crosses_into_a_new_session) {
    for (auto k : backends()) {
        Link l(k);
        confirm_both(l);
        l.start_client();
        CHECK(l.pump([&] { return l.both(DtlsState::Authenticated); }, 10000));
        const auto old = l.client_rx;
        l.client_t->close_peer(l.to_host);
        l.client_s.reset();
        l.start_client();
        int replayed = 0;
        for (const auto& d : old)
            if (!epoch0(d)) {  // the encrypted ones: binding, ack (handshake flights: the next test)
                l.client_s->on_datagram(d, now_us(), now_unix());
                ++replayed;
            }
        CHECK(replayed >= 1);
        CHECK(l.client_s->state() != DtlsState::Authenticated);
        CHECK(l.client_s->stats().binding_acks_received == 0);
        const auto* first_host = l.host_s.get();
        CHECK(l.pump([&] { return l.both(DtlsState::Authenticated) && l.host_s.get() != first_host; }, 10000));
        l.report("dtls_confirm_binding_an_old_sessions_ack_never_crosses_into_a_new_session");
        CHECK(l.client_s->stats().gate_timeouts == 0);
        CHECK(l.client_s->confirmation() == l.host_s->confirmation());
    }
}


// Found while writing the test above (KI-44, independent of BindingAck: the same with the flag off): the previous
// session's PLAINTEXT handshake flights injected into a new handshake make it fail closed. The client is rejected
// (handshake_failed), never authenticated by them; that connection attempt is lost and the client retries.
TEST(dtls_old_handshake_flights_injected_into_a_new_handshake_fail_closed) {
    for (auto k : backends()) {
        for (const bool confirm : {false, true}) {
            Link l(k);
            if (confirm) confirm_both(l);
            l.start_client();
            CHECK(l.pump([&] { return l.both(DtlsState::Authenticated); }, 10000));
            const auto old = l.client_rx;
            l.client_t->close_peer(l.to_host);
            l.client_s.reset();
            l.start_client();
            for (const auto& d : old)
                if (epoch0(d)) l.client_s->on_datagram(d, now_us(), now_unix());
            bool ever_authenticated = false;
            l.pump([&] {
                ever_authenticated |= l.client_s->state() == DtlsState::Authenticated;
                return l.client_s->state() == DtlsState::Rejected;
            }, 6000);
            l.report(confirm ? "old flights (confirm on)" : "old flights (confirm off)");
            CHECK(!ever_authenticated);
            CHECK(l.client_s->state() == DtlsState::Rejected);
            CHECK(l.client_s->stats().app_in == 0);
        }
    }
}
