// Secure transport decorator (review #7 answer 3, FB-7-01, FB-7-05): Host/Client game traffic through DTLS with
// pinned keys and the invitation binding, on every backend, plus the bounded pre-authentication lifecycle.
#include <openssl/evp.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <memory>

#include "network/dtls_session.hpp"
#include "network/secure_transport.hpp"
#include "test_support.hpp"

using namespace mordor;
using namespace mordor::net;
using client::ClientState;

namespace {

constexpr std::uint64_t kSec = 1'000'000;

struct Key {
    EVP_PKEY* p = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
    Key() { if (!p) throw test::CheckFailure("keygen failed"); }
    ~Key() { EVP_PKEY_free(p); }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    std::vector<std::uint8_t> spki() const { return spki_der(p); }
};

std::vector<TransportKind> backends() {
    std::vector<TransportKind> out;
    for (auto k : {TransportKind::Udp, TransportKind::Gns})
        if (transport_available(k)) out.push_back(k);
    return out;
}

Invitation invite(const char* id = "invite-0001") {
    return {id, kProtocolVersion, static_cast<std::uint64_t>(std::time(nullptr)) + 3600};
}

std::shared_ptr<SecureTransportConfig> secure(const Key& own, std::vector<std::vector<std::uint8_t>> pins, Invitation inv) {
    auto c = std::make_shared<SecureTransportConfig>();
    c->own_key = own.p;
    c->pinned_peers = std::move(pins);
    c->invitation = std::move(inv);
    return c;
}

// Sim clock for the session layer, real time underneath (DTLS and GNS timers): 1 ms real per 5 ms sim step.
bool pump_rt(test::SimClock& clk, host::Host* h, std::vector<client::Client*> cs, const std::function<bool()>& done,
          std::uint64_t max_sim_us) {
    const auto deadline = clk.now + max_sim_us;
    while (clk.now < deadline) {
        if (h) h->update(clk.now);
        for (auto* c : cs) c->update(clk.now);
        if (done()) return true;
        clk.now += 5'000;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

struct World {
    TransportKind kind;
    Key host_key, k1, k2;
    Invitation inv = invite();
    test::SimClock clk;
    std::unique_ptr<host::Host> host;
    std::vector<std::unique_ptr<client::Client>> clients;
    explicit World(TransportKind k, bool secure_host = true, host::HostConfig hc = {}) : kind(k) {
        hc.transport = k;
        if (secure_host) hc.secure = secure(host_key, {k1.spki(), k2.spki()}, inv);
        host = std::make_unique<host::Host>(hc, test::test_logger("host"));
        std::string err;
        if (!host->start(&err)) throw test::CheckFailure("host start: " + err);
    }
    client::Client& add(const Key* key, std::vector<std::vector<std::uint8_t>> pins, client::ClientConfig cc = {}, Invitation i = {}) {
        cc.host = Endpoint::loopback(host->port());
        cc.transport = kind;
        if (key) cc.secure = secure(*key, std::move(pins), i.id.empty() ? inv : i);
        clients.push_back(std::make_unique<client::Client>(cc, test::test_logger("client")));
        std::string err;
        if (!clients.back()->start(&err)) throw test::CheckFailure("client start: " + err);
        clients.back()->connect(clk.now);
        return *clients.back();
    }
    std::vector<client::Client*> ptrs() {
        std::vector<client::Client*> v;
        for (auto& c : clients) v.push_back(c.get());
        return v;
    }
    bool run_until(const std::function<bool()>& done, std::uint64_t sim_us) { return pump_rt(clk, host.get(), ptrs(), done, sim_us); }
};

}  // namespace

TEST(secure_game_session_runs_over_dtls_on_every_backend) {
    for (auto k : backends()) {
        World w(k);
        auto& a = w.add(&w.k1, {w.host_key.spki()});
        auto& b = w.add(&w.k2, {w.host_key.spki()});
        CHECK(w.run_until([&] { return a.state() == ClientState::Connected && b.state() == ClientState::Connected; }, 10 * kSec));
        a.set_move(1, 0);
        CHECK(w.run_until([&] { return a.stats().snapshots_applied > 20 && b.remote_entities().size() == 1; }, 5 * kSec));
        const auto* hs = secure_stats(*w.host->transport_for_test());
        CHECK(hs && hs->authenticated == 2 && hs->dropped_not_dtls == 0);
        std::printf("       %s: 2 players over DTLS, %llu snapshots at client a\n", to_string(k),
                    static_cast<unsigned long long>(a.stats().snapshots_applied));
    }
}

TEST(secure_host_never_serves_a_plaintext_client) {  // no plaintext fallback (review #7 answer 3)
    for (auto k : backends()) {
        World w(k);
        client::ClientConfig cc;
        cc.connect_deadline_us = 3 * kSec;
        auto& plain = w.add(nullptr, {}, cc);
        CHECK(w.run_until([&] { return plain.state() == ClientState::Failed; }, 5 * kSec));
        CHECK(w.host->connected_count() == 0);
        const auto* hs = secure_stats(*w.host->transport_for_test());
        std::printf("       %s: plaintext client %s (%s); host dropped %llu non-DTLS datagrams\n", to_string(k),
                    client::to_string(plain.state()), plain.state_reason().c_str(),
                    static_cast<unsigned long long>(hs->dropped_not_dtls));
        CHECK(hs->dropped_not_dtls >= 1 && hs->authenticated == 0);
    }
}

TEST(secure_client_never_talks_to_a_plaintext_host) {
    for (auto k : backends()) {
        World w(k, /*secure_host=*/false);
        client::ClientConfig cc;
        cc.connect_deadline_us = 3 * kSec;
        auto& c = w.add(&w.k1, {w.host_key.spki()}, cc);
        CHECK(w.run_until([&] { return c.state() == ClientState::Failed; }, 5 * kSec));
        CHECK(w.host->connected_count() == 0);
        std::printf("       %s: secure client vs plaintext host -> %s (%s)\n", to_string(k), client::to_string(c.state()),
                    c.state_reason().c_str());
    }
}

TEST(secure_host_refuses_an_unpinned_player_and_a_wrong_invitation) {
    for (auto k : backends()) {
        World w(k);
        Key stranger;
        client::ClientConfig cc;
        cc.connect_deadline_us = 4 * kSec;
        auto& s = w.add(&stranger, {w.host_key.spki()}, cc);
        auto& wrong = w.add(&w.k1, {w.host_key.spki()}, cc, invite("invite-9999"));
        CHECK(w.run_until([&] { return s.state() == ClientState::Failed && wrong.state() == ClientState::Failed; }, 6 * kSec));
        CHECK(w.host->connected_count() == 0 && w.host->slot_count() == 0);
        std::printf("       %s: unpinned -> %s, wrong invitation -> %s\n", to_string(k), s.state_reason().c_str(),
                    wrong.state_reason().c_str());
    }
}

// Session-level reconnect through the decorator: a new DTLS handshake under a NEW transport PeerId, then the
// game token restores the same player.
TEST(secure_reconnect_after_outage_keeps_identity) {
    for (auto k : backends()) {
        World w(k);
        auto& c = w.add(&w.k1, {w.host_key.spki()});
        CHECK(w.run_until([&] { return c.state() == ClientState::Connected; }, 10 * kSec));
        const auto player = c.player_id();
        ImpairmentConfig cut;
        cut.loss = 1.0;
        CHECK(c.set_impairment(cut));
        CHECK(w.host->set_impairment(cut));
        CHECK(w.run_until([&] { return w.host->connected_count() == 0; }, 8 * kSec));
        CHECK(c.set_impairment({}));
        CHECK(w.host->set_impairment({}));
        CHECK(w.run_until([&] { return c.state() == ClientState::Connected && c.stats().reconnects >= 1; }, 15 * kSec));
        CHECK(c.player_id() == player);
        const auto* hs = secure_stats(*w.host->transport_for_test());
        std::printf("       %s: reconnected as player %u; host authenticated %llu sessions, %llu replaced\n", to_string(k),
                    c.player_id(), static_cast<unsigned long long>(hs->authenticated),
                    static_cast<unsigned long long>(hs->promotions_replacing));
        CHECK(hs->authenticated >= 2);
    }
}

// ---- Decorator lifecycle at transport level (FB-7-01 / FB-7-05) ----
namespace {
std::unique_ptr<Transport> open_secure(TransportKind k, const SecureTransportConfig& cfg, bool listen, ImpairmentConfig imp = {}) {
    std::string err;
    auto t = make_secure_transport(make_transport(k, imp, &err), cfg, &err);
    if (!t || !t->open(Endpoint::loopback(0), listen, &err)) throw test::CheckFailure("secure open: " + err);
    return t;
}
std::uint64_t real_us() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
// Polls all transports in real time; collects events of `watch`.
template <class Pred>
std::vector<TransportEvent> run(std::vector<Transport*> all, Transport* watch, Pred done, int max_ms,
                                std::vector<DtlsSession*> extra = {}) {
    std::vector<TransportEvent> seen;
    for (int i = 0; i < max_ms; ++i) {
        for (auto* t : all) {
            std::vector<TransportEvent> ev;
            t->poll(real_us(), ev);
            if (t == watch) seen.insert(seen.end(), ev.begin(), ev.end());
            // Raw transports drive DTLS sessions of their own (attackers): hand them their datagrams. extra[i] runs
            // over all[i + 1] by construction in the tests that use it.
            for (std::size_t j = 0; j < extra.size(); ++j)
                if (j + 1 < all.size() && all[j + 1] == t)
                    for (const auto& e : ev)
                        if (e.kind == TransportEvent::Kind::Message)
                            extra[j]->on_datagram(e.bytes, real_us(), static_cast<std::uint64_t>(std::time(nullptr)));
        }
        for (auto* s : extra) s->service(real_us(), static_cast<std::uint64_t>(std::time(nullptr)));
        if (done(seen)) return seen;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return seen;
}
std::size_t count(const std::vector<TransportEvent>& ev, TransportEvent::Kind k) {
    return static_cast<std::size_t>(std::count_if(ev.begin(), ev.end(), [&](const auto& e) { return e.kind == k; }));
}
}  // namespace

// Pre-authentication state is bounded: more unpinned handshakes than max_pending_total are refused, each pending
// session expires, and afterwards a legitimate player still gets in.
TEST(secure_pending_sessions_are_capped_and_expire) {
    for (auto k : backends()) {
        Key host_key, player, attacker;
        auto hcfg = *secure(host_key, {player.spki()}, invite());
        hcfg.max_pending_total = 3;
        hcfg.pending_lifetime_us = 1'500'000;
        auto host = open_secure(k, hcfg, true);
        // Attackers: plain backend transports driving a DTLS client with an unpinned key (real ClientHellos).
        std::vector<std::unique_ptr<Transport>> raw;
        std::vector<std::unique_ptr<DtlsSession>> hello;
        std::vector<Transport*> all{host.get()};
        std::vector<DtlsSession*> sessions;
        for (int i = 0; i < 6; ++i) {
            std::string err;
            raw.push_back(make_transport(k, {}, &err));
            CHECK(raw.back()->open(Endpoint::loopback(0), false, &err));
            const auto p = raw.back()->connect(Endpoint::loopback(host->local_endpoint().port)).value();
            DtlsConfig d;
            d.own_key = attacker.p;
            d.pinned_peers = {host_key.spki()};
            d.invitation = hcfg.invitation;
            hello.push_back(std::make_unique<DtlsSession>(*raw.back(), p, Endpoint::loopback(host->local_endpoint().port), d));
            all.push_back(raw.back().get());
            sessions.push_back(hello.back().get());
        }
        const auto* hs = secure_stats(*host);
        run(all, host.get(), [&](const auto&) { return hs->pending_opened >= 3 && hs->pending_refused_cap >= 1; }, 3000, sessions);
        std::printf("       %s: opened %llu pending, refused %llu by the cap (max 3)\n", to_string(k),
                    static_cast<unsigned long long>(hs->pending_opened), static_cast<unsigned long long>(hs->pending_refused_cap));
        CHECK(hs->pending_refused_cap >= 1);
        CHECK(hs->authenticated == 0);
        hello.clear();  // attackers go silent; their pending sessions must expire
        run(all, host.get(), [&](const auto&) { return hs->pending_failed >= hs->pending_opened; }, 5000);
        CHECK(hs->pending_failed >= hs->pending_opened);
        auto ccfg = *secure(player, {host_key.spki()}, hcfg.invitation);
        auto c = open_secure(k, ccfg, false);
        const auto to_host = c->connect(Endpoint::loopback(host->local_endpoint().port)).value();
        all.push_back(c.get());
        const auto ev = run(all, c.get(), [&](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; }, 5000);
        CHECK(count(ev, TransportEvent::Kind::Connected) == 1);
        CHECK(c->send(to_host, std::vector<std::uint8_t>{7}, Delivery::Unreliable, 0) == SendResult::Sent);
        // Review #8 FB-8-02: ReliableOrdered is now the decorator's own secure channel (was Unsupported before).
        CHECK(c->send(to_host, std::vector<std::uint8_t>{7}, Delivery::ReliableOrdered, 0) == SendResult::Sent);
    }
}

// UDP: reconnects from the SAME address. Each one replaces the association only once it authenticates; the host
// reports the old peer Disconnected ("replaced") and the new one Connected under a new PeerId; a record of the old
// session that arrives late is dropped; a reconnect with a different invitation never replaces anything.
TEST(secure_same_address_reconnects_replace_only_when_authenticated) {
    Key host_key, player;
    auto hcfg = *secure(host_key, {player.spki()}, invite());
    auto host = open_secure(TransportKind::Udp, hcfg, true);
    ImpairmentConfig slow;  // client datagrams take 300 ms: an old-session record can land after a promotion
    slow.latency_ms = 300;
    auto ccfg = *secure(player, {host_key.spki()}, hcfg.invitation);
    auto c = open_secure(TransportKind::Udp, ccfg, false, slow);
    const Endpoint host_ep = Endpoint::loopback(host->local_endpoint().port);
    std::vector<TransportEvent> host_events;
    auto collect = [&](int ms, auto done) {
        auto ev = run({host.get(), c.get()}, host.get(), [&](const auto& e) { return done(e); }, ms);
        host_events.insert(host_events.end(), ev.begin(), ev.end());
        return ev;
    };
    auto peer = c->connect(host_ep).value();
    collect(5000, [](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; });
    const auto* hs = secure_stats(*host);
    CHECK(hs->authenticated == 1);
    for (int round = 1; round <= 2; ++round) {
        CHECK(c->send(peer, std::vector<std::uint8_t>{static_cast<std::uint8_t>(100 + round)}, Delivery::Unreliable, 0) == SendResult::Sent);
        c->close_peer(peer);  // the old record is still in flight (300 ms)
        peer = c->connect(host_ep).value();
        collect(6000, [&](const auto&) { return hs->authenticated == static_cast<std::uint64_t>(1 + round); });
    }
    collect(800, [](const auto&) { return false; });  // let every delayed old record arrive
    CHECK(hs->authenticated == 3 && hs->promotions_replacing == 2);
    std::size_t replaced = 0;
    for (const auto& e : host_events) replaced += e.kind == TransportEvent::Kind::Disconnected && e.reason == "replaced_by_new_session";
    CHECK(replaced == 2);
    // A late record of a retired session never surfaces under the new peer (or at all, once retired).
    const auto last_connected = std::find_if(host_events.rbegin(), host_events.rend(),
                                             [](const auto& e) { return e.kind == TransportEvent::Kind::Connected; });
    for (auto it = host_events.rbegin(); it != last_connected; ++it) CHECK(it->kind != TransportEvent::Kind::Message);
    // A player with a different invitation (another socket): refused; the established association is untouched.
    auto wrong_cfg = ccfg;
    wrong_cfg.invitation.id = "invite-9999";
    auto w = open_secure(TransportKind::Udp, wrong_cfg, false);
    w->connect(host_ep);
    run({host.get(), c.get(), w.get()}, host.get(), [&](const auto&) { return false; }, 1500);
    CHECK(hs->authenticated == 3);
    CHECK(c->send(peer, std::vector<std::uint8_t>{9}, Delivery::Unreliable, 0) == SendResult::Sent);
    const auto ev = run({host.get(), c.get()}, host.get(), [](const auto& e) { return count(e, TransportEvent::Kind::Message) >= 1; }, 3000);
    CHECK(std::any_of(ev.begin(), ev.end(), [](const auto& e) {
        return e.kind == TransportEvent::Kind::Message && e.bytes == std::vector<std::uint8_t>{9};
    }));
    std::printf("       udp: 2 same-address reconnects replaced the association; late old records dropped; wrong invitation ignored\n");
}

// ---- Review #8 FB-8-01: forged ClientHellos from the player address must not starve its reconnect. ----
namespace {
// Shares one real inner transport with the test, so the test can inject datagrams from the client address.
struct Tap final : Transport {
    std::shared_ptr<Transport> t;
    PeerId last_peer{};
    explicit Tap(std::shared_ptr<Transport> inner) : t(std::move(inner)) {}
    bool open(const Endpoint& b, bool l, std::string* e) override { return t->open(b, l, e); }
    void close() override { t->close(); }
    bool is_open() const override { return t->is_open(); }
    Endpoint local_endpoint() const override { return t->local_endpoint(); }
    TransportCapabilities capabilities() const override { return t->capabilities(); }
    std::optional<PeerId> connect(const Endpoint& r) override {
        auto p = t->connect(r);
        if (p) last_peer = *p;
        return p;
    }
    SendResult send(PeerId p, std::span<const std::uint8_t> b, Delivery d, std::uint64_t n) override {
        sent_first_bytes.push_back(b.empty() ? 0 : b[0]);
        sent_tail.emplace_back(b.begin(), b.end());
        if (sent_tail.size() > 16) sent_tail.erase(sent_tail.begin());
        return t->send(p, b, d, n);
    }
    void poll(std::uint64_t n, std::vector<TransportEvent>& out) override { t->poll(n, out); }
    void flush(std::uint64_t n) override { t->flush(n); }
    void close_peer(PeerId p) override { t->close_peer(p); }
    void flush_all() override { t->flush_all(); }
    bool set_impairment(const ImpairmentConfig& c) override { return t->set_impairment(c); }
    TransportKind kind() const override { return t->kind(); }
    const TransportStats& stats() const override { return t->stats(); }
    std::vector<std::uint8_t> sent_first_bytes;  // what the decorator put on the wire (record content types)
    std::vector<std::vector<std::uint8_t>> sent_tail;  // the last datagrams sent (for replay tests)
};
// Records what a DTLS client would send, without a network: a source of realistic forged ClientHellos.
struct Capture final : Transport {
    std::vector<std::vector<std::uint8_t>> sent;
    TransportStats s;
    bool open(const Endpoint&, bool, std::string*) override { return true; }
    void close() override {}
    bool is_open() const override { return true; }
    Endpoint local_endpoint() const override { return {}; }
    TransportCapabilities capabilities() const override { return {.max_payload = kMaxPacketSize}; }
    std::optional<PeerId> connect(const Endpoint&) override { return PeerId{1, 0}; }
    SendResult send(PeerId, std::span<const std::uint8_t> b, Delivery, std::uint64_t) override {
        sent.emplace_back(b.begin(), b.end());
        return SendResult::Sent;
    }
    void poll(std::uint64_t, std::vector<TransportEvent>&) override {}
    void flush(std::uint64_t) override {}
    void close_peer(PeerId) override {}
    void flush_all() override {}
    bool set_impairment(const ImpairmentConfig&) override { return true; }
    TransportKind kind() const override { return TransportKind::Udp; }
    const TransportStats& stats() const override { return s; }
};
std::vector<std::uint8_t> forged_client_hello(const Key& stranger, const std::vector<std::uint8_t>& host_spki, const Invitation& inv) {
    Capture cap;
    DtlsConfig d;
    d.own_key = stranger.p;
    d.pinned_peers = {host_spki};
    d.invitation = inv;
    DtlsSession s(cap, PeerId{1, 0}, Endpoint{}, d);
    s.service(real_us(), static_cast<std::uint64_t>(std::time(nullptr)));  // first flight: one fresh ClientHello
    if (cap.sent.empty() || !dtls_parse_client_hello(cap.sent[0])) throw test::CheckFailure("no ClientHello captured");
    return cap.sent[0];
}
}  // namespace

TEST(secure_forged_client_hellos_cannot_starve_a_players_reconnect) {
    Key host_key, player, stranger;
    auto hcfg = *secure(host_key, {player.spki()}, invite());
    hcfg.max_pending_total = 2;
    auto host = open_secure(TransportKind::Udp, hcfg, true);
    std::string err;
    std::shared_ptr<Transport> shared = make_transport(TransportKind::Udp, {}, &err);
    auto tap_owned = std::make_unique<Tap>(shared);
    Tap* tap = tap_owned.get();
    auto c = make_secure_transport(std::move(tap_owned), *secure(player, {host_key.spki()}, hcfg.invitation), &err);
    CHECK(c && c->open(Endpoint::loopback(0), false, &err));
    const Endpoint host_ep = Endpoint::loopback(host->local_endpoint().port);
    auto peer = c->connect(host_ep).value();
    std::vector<TransportEvent> hev;
    auto step = [&](int ms, auto done) {
        auto ev = run({host.get(), c.get()}, host.get(), [&](const auto& e) { return done(e); }, ms);
        hev.insert(hev.end(), ev.begin(), ev.end());
    };
    step(5000, [](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; });
    const auto* hs = secure_stats(*host);
    CHECK(hs->authenticated == 1);
    // Strangers from other addresses fill the global pending cap while the player is active.
    std::vector<std::unique_ptr<Transport>> others;
    for (int i = 0; i < 3; ++i) {
        others.push_back(make_transport(TransportKind::Udp, {}, &err));
        CHECK(others.back()->open(Endpoint::loopback(0), false, &err));
        const auto p = others.back()->connect(host_ep).value();
        others.back()->send(p, forged_client_hello(stranger, host_key.spki(), hcfg.invitation), Delivery::Unreliable, 0);
    }
    // Forged ClientHellos (fresh randoms) injected from the PLAYER address, continuously.
    auto spam = [&] { shared->send(tap->last_peer, forged_client_hello(stranger, host_key.spki(), hcfg.invitation), Delivery::Unreliable, 0); };
    for (int i = 0; i < 5; ++i) {
        spam();
        step(20, [](const auto&) { return false; });
    }
    // KI-30 stateless cookie gate (review #10 answer 5): forged ClientHellos never complete the cookie exchange, so
    // they get a HelloVerifyRequest and allocate NOTHING (before: each one created or replaced a pending session).
    const auto opened_before = hs->pending_opened;
    CHECK(hs->hello_verify_sent >= 8 && hs->pending_refused_cap == 0);
    CHECK(c->send(peer, std::vector<std::uint8_t>{5}, Delivery::Unreliable, 0) == SendResult::Sent);  // the player still plays
    step(2000, [](const auto& e) {
        return std::any_of(e.begin(), e.end(), [](const auto& x) { return x.kind == TransportEvent::Kind::Message; });
    });
    CHECK(std::any_of(hev.begin(), hev.end(), [](const auto& x) { return x.kind == TransportEvent::Kind::Message && x.bytes[0] == 5; }));
    // Legitimate reconnect from the same address while a forgery is injected on EVERY 1 ms step, so some land
    // between the player handshake proving its cookie and its completion.
    const auto t0 = std::chrono::steady_clock::now();
    c->close_peer(peer);
    peer = c->connect(host_ep).value();
    for (int i = 0; i < 5000 && hs->authenticated < 2; ++i) {
        spam();
        step(1, [&](const auto&) { return hs->authenticated >= 2; });
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("       udp: reconnect authenticated after %lld ms under forged ClientHellos (verify requests %llu, pending opened by forgeries %llu)\n",
                static_cast<long long>(ms), static_cast<unsigned long long>(hs->hello_verify_sent),
                static_cast<unsigned long long>(hs->pending_opened - opened_before - 1));
    CHECK(hs->authenticated == 2);
    CHECK(ms < 3000);  // was bounded only by the 10 s pending lifetime before FB-8-01
    CHECK(hs->pending_opened == opened_before + 1);  // only the player's own, cookie-proven attempt allocated state
    // Event order at the host: the old association is retired before the new one is reported.
    std::size_t disc = SIZE_MAX, conn = SIZE_MAX;
    for (std::size_t i = 0; i < hev.size(); ++i) {
        if (hev[i].kind == TransportEvent::Kind::Disconnected && hev[i].reason == "replaced_by_new_session") disc = i;
        if (hev[i].kind == TransportEvent::Kind::Connected) conn = i;  // last Connected
    }
    CHECK(disc != SIZE_MAX && conn != SIZE_MAX && disc < conn);
    // FB-8-02 check on the same link: nothing but DTLS records ever left the client (content types 20..23).
    for (auto b : tap->sent_first_bytes) CHECK(b >= 20 && b <= 23);
    std::printf("       udp: %zu datagrams from the secure client, all DTLS records\n", tap->sent_first_bytes.size());
}

// Review #8 FB-8-04: a peer that goes silent is reported by the decorator itself on every backend.
TEST(secure_silent_peer_is_reported_after_idle_timeout) {
    for (auto k : backends()) {
        Key host_key, player;
        auto hcfg = *secure(host_key, {player.spki()}, invite());
        hcfg.idle_timeout_us = 1'000'000;
        auto host = open_secure(k, hcfg, true);
        auto c = open_secure(k, *secure(player, {host_key.spki()}, hcfg.invitation), false);
        c->connect(Endpoint::loopback(host->local_endpoint().port));
        run({host.get(), c.get()}, host.get(), [](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; }, 5000);
        CHECK(secure_stats(*host)->authenticated == 1);
        const auto t0 = std::chrono::steady_clock::now();
        // The client process stops (never polled again): nothing more reaches the host.
        const auto ev = run({host.get()}, host.get(), [](const auto& e) {
            return std::any_of(e.begin(), e.end(), [](const auto& x) { return x.kind == TransportEvent::Kind::Disconnected; });
        }, 5000);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        const auto it = std::find_if(ev.begin(), ev.end(), [](const auto& x) { return x.kind == TransportEvent::Kind::Disconnected; });
        CHECK(it != ev.end());
        std::printf("       %s: silent peer reported after %lld ms (%s)\n", to_string(k), static_cast<long long>(ms),
                    it == ev.end() ? "-" : std::string(it->reason).c_str());
        CHECK(it != ev.end() && it->reason == "idle_timeout" && ms >= 900 && ms < 2500);
        CHECK(secure_stats(*host)->idle_timeouts == 1);
    }
}

// ---- Review #8 FB-8-02: secure reliable-ordered channel inside the authenticated session, every backend. ----
namespace {
std::vector<std::uint8_t> tagged(std::uint8_t tag, std::uint32_t seq) {
    std::vector<std::uint8_t> b(5, tag);
    std::memcpy(b.data() + 1, &seq, 4);
    return b;
}
std::uint32_t seq_of(const TransportEvent& e) {
    std::uint32_t v = 0;
    if (e.bytes.size() >= 5) std::memcpy(&v, e.bytes.data() + 1, 4);
    return v;
}
struct SecureLink {
    Key host_key, player;
    std::unique_ptr<Transport> host, client;
    PeerId to_host{}, to_client{};
    SecureLink(TransportKind k, ImpairmentConfig imp = {}, std::uint64_t idle_us = 10'000'000) {
        auto hcfg = *secure(host_key, {player.spki()}, invite());
        hcfg.idle_timeout_us = idle_us;
        auto ccfg = *secure(player, {host_key.spki()}, hcfg.invitation);
        ccfg.idle_timeout_us = idle_us;
        ImpairmentConfig cimp = imp;
        cimp.seed = imp.seed + 7;
        host = open_secure(k, hcfg, true, imp);
        client = open_secure(k, ccfg, false, cimp);  // GNS: same config joins the process-global holder
        to_host = client->connect(Endpoint::loopback(host->local_endpoint().port)).value();
        const auto ev = run({host.get(), client.get()}, host.get(),
                            [](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; }, 15000);
        for (const auto& e : ev)
            if (e.kind == TransportEvent::Kind::Connected) to_client = e.peer;
        run({host.get(), client.get()}, client.get(), [](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; }, 5000);
        if (!to_client.valid()) throw test::CheckFailure("secure link did not authenticate");
    }
    ~SecureLink() {
        if (host) host->set_impairment({});
        if (client) client->set_impairment({});
    }
};
}  // namespace

TEST(secure_reliable_is_exact_and_ordered_under_loss_both_directions) {
    for (auto k : backends()) {
        ImpairmentConfig bad;
        bad.loss = 0.15;
        bad.duplicate = 0.1;
        bad.latency_ms = 10;
        bad.jitter_ms = 20;
        bad.seed = 91;
        SecureLink l(k, bad);
        constexpr std::uint32_t kN = 300;  // more than the 256-message window: senders must respect Backpressure
        std::uint32_t sent_c = 0, sent_h = 0, at_host = 0, at_client = 0;
        bool ordered = true;
        for (int i = 0; i < 30000 && (at_host < kN || at_client < kN); ++i) {
            while (sent_c < kN && l.client->send(l.to_host, tagged(1, sent_c + 1), Delivery::ReliableOrdered, 0) == SendResult::Sent) ++sent_c;
            while (sent_h < kN && l.host->send(l.to_client, tagged(2, sent_h + 1), Delivery::ReliableOrdered, 0) == SendResult::Sent) ++sent_h;
            for (auto* t : {l.host.get(), l.client.get()}) {
                std::vector<TransportEvent> ev;
                t->poll(real_us(), ev);
                for (const auto& e : ev) {
                    if (e.kind != TransportEvent::Kind::Message || e.delivery != Delivery::ReliableOrdered) continue;
                    auto& n = t == l.host.get() ? at_host : at_client;
                    ordered = ordered && seq_of(e) == n + 1;
                    ++n;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto* hs = secure_stats(*l.host);
        std::printf("       %s: %u/%u each way, in order; host retransmits %llu, duplicates dropped %llu\n", to_string(k), at_host,
                    at_client, static_cast<unsigned long long>(hs->reliable_retransmits), static_cast<unsigned long long>(hs->reliable_duplicates));
        CHECK(ordered && at_host == kN && at_client == kN);  // exact: nothing lost, nothing twice, nothing reordered
    }
}

TEST(secure_reliable_window_reports_backpressure_and_delivers_everything_accepted) {
    for (auto k : backends()) {
        SecureLink l(k);
        std::uint32_t accepted = 0, refused = 0;
        for (int i = 0; i < 1000 && refused == 0; ++i) {
            const auto r = l.client->send(l.to_host, tagged(3, accepted + 1), Delivery::ReliableOrdered, 0);
            if (r == SendResult::Sent) ++accepted;
            else if (r == SendResult::Backpressure) ++refused;
            else CHECK(r == SendResult::Sent);
        }
        CHECK(refused == 1 && accepted == 256);  // bounded window, explicit pushback
        std::uint32_t got = 0;
        bool ordered = true;
        run({l.host.get(), l.client.get()}, l.host.get(), [&](const auto& e) {
            got = 0;
            for (const auto& x : e)
                if (x.kind == TransportEvent::Kind::Message) {
                    ordered = ordered && seq_of(x) == got + 1;
                    ++got;
                }
            return got == accepted;
        }, 10000);
        CHECK(ordered && got == accepted);
        CHECK(l.client->send(l.to_host, tagged(3, 999), Delivery::ReliableOrdered, 0) == SendResult::Sent);  // window freed
    }
}

TEST(secure_reliable_interleaves_with_unreliable_snapshots) {
    for (auto k : backends()) {
        SecureLink l(k);
        for (std::uint32_t i = 1; i <= 100; ++i) {
            CHECK(l.client->send(l.to_host, tagged(4, i), Delivery::ReliableOrdered, 0) == SendResult::Sent);
            CHECK(l.client->send(l.to_host, tagged(5, i), Delivery::Unreliable, 0) == SendResult::Sent);
        }
        std::uint32_t rel = 0, unrel = 0;
        bool ordered = true;
        run({l.host.get(), l.client.get()}, l.host.get(), [&](const auto& e) {
            rel = unrel = 0;
            ordered = true;
            for (const auto& x : e) {
                if (x.kind != TransportEvent::Kind::Message) continue;
                if (x.bytes[0] == 4) {
                    ordered = ordered && x.delivery == Delivery::ReliableOrdered && seq_of(x) == rel + 1;
                    ++rel;
                } else {
                    ordered = ordered && x.delivery == Delivery::Unreliable;
                    ++unrel;
                }
            }
            return rel == 100;
        }, 5000);
        CHECK(ordered && rel == 100 && unrel >= 90);
    }
}

// Accepted reliable data that can never be acknowledged is reported, not silently lost.
TEST(secure_reliable_lost_with_its_session_is_counted) {
    for (auto k : backends()) {
        SecureLink l(k, {}, 1'000'000);
        ImpairmentConfig cut;
        cut.loss = 1.0;
        CHECK(l.client->set_impairment(cut));
        if (l.host->capabilities().per_instance_impairment) CHECK(l.host->set_impairment(cut));
        for (std::uint32_t i = 1; i <= 5; ++i) CHECK(l.client->send(l.to_host, tagged(6, i), Delivery::ReliableOrdered, 0) == SendResult::Sent);
        const auto ev = run({l.host.get(), l.client.get()}, l.client.get(),
                            [](const auto& e) { return count(e, TransportEvent::Kind::Disconnected) == 1; }, 5000);
        CHECK(count(ev, TransportEvent::Kind::Disconnected) == 1);
        std::printf("       %s: session lost with 5 unacknowledged reliable messages -> accepted_then_failed %llu\n", to_string(k),
                    static_cast<unsigned long long>(l.client->stats().accepted_then_failed));
        CHECK(l.client->stats().accepted_then_failed == 5);
    }
}

// close() drains for a BOUNDED time and counts what stayed unacknowledged. (Here the host is not polled during
// the drain - one thread - so no ack can arrive; delivery with acks is covered by the multi-process secure E2E.)
TEST(secure_close_drain_is_bounded_and_counts_unacked) {
    for (auto k : backends()) {
        SecureLink l(k);
        CHECK(l.client->send(l.to_host, tagged(7, 1), Delivery::ReliableOrdered, 0) == SendResult::Sent);
        const auto t0 = std::chrono::steady_clock::now();
        l.client->close();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        const auto ev = run({l.host.get()}, l.host.get(), [](const auto& e) { return count(e, TransportEvent::Kind::Message) >= 1; }, 3000);
        std::printf("       %s: close() drained for %lld ms; message delivered: %s\n", to_string(k), static_cast<long long>(ms),
                    count(ev, TransportEvent::Kind::Message) ? "yes" : "no");
        CHECK(ms >= 450 && ms < 1000);
        CHECK(l.client->stats().closed_with_unacked_reliable == 1);
        CHECK(count(ev, TransportEvent::Kind::Message) >= 1);  // its (re)transmissions did reach the host
    }
}

// FB-8-04: a quiet but alive peer is kept (keepalives); only a vanished one times out.
TEST(secure_quiet_peer_is_kept_alive) {
    for (auto k : backends()) {
        SecureLink l(k, {}, 1'000'000);
        const auto ev = run({l.host.get(), l.client.get()}, l.host.get(), [](const auto&) { return false; }, 3000);
        CHECK(count(ev, TransportEvent::Kind::Disconnected) == 0);
        CHECK(l.client->send(l.to_host, tagged(8, 1), Delivery::ReliableOrdered, 0) == SendResult::Sent);
    }
}

// Review #9 FB-9-01: datagrams that are not authenticated records must not keep a vanished peer alive.
TEST(secure_spoofed_or_replayed_datagrams_do_not_keep_a_vanished_peer_alive) {
    Key host_key, player;
    auto hcfg = *secure(host_key, {player.spki()}, invite());
    hcfg.idle_timeout_us = 1'000'000;
    auto host = open_secure(TransportKind::Udp, hcfg, true);
    std::string err;
    std::shared_ptr<Transport> shared = make_transport(TransportKind::Udp, {}, &err);
    auto tap_owned = std::make_unique<Tap>(shared);
    Tap* tap = tap_owned.get();
    auto ccfg = *secure(player, {host_key.spki()}, hcfg.invitation);
    ccfg.idle_timeout_us = 1'000'000;
    auto c = make_secure_transport(std::move(tap_owned), ccfg, &err);
    CHECK(c && c->open(Endpoint::loopback(0), false, &err));
    c->connect(Endpoint::loopback(host->local_endpoint().port));
    run({host.get(), c.get()}, host.get(), [](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; }, 5000);
    run({host.get(), c.get()}, host.get(), [](const auto&) { return false; }, 200);  // a few real records flow
    const auto replay = tap->sent_tail;  // genuine, already-delivered DTLS records of this association
    CHECK(!replay.empty());
    // The real client vanishes (never polled again). From its address: garbage, replays, a forged ClientHello-like blob.
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<TransportEvent> hev;
    for (int i = 0; i < 300 && count(hev, TransportEvent::Kind::Disconnected) == 0; ++i) {
        std::vector<std::uint8_t> junk(40, static_cast<std::uint8_t>(i));
        junk[0] = 23;  // looks like DTLS application data
        shared->send(tap->last_peer, junk, Delivery::Unreliable, 0);
        shared->send(tap->last_peer, replay[static_cast<std::size_t>(i) % replay.size()], Delivery::Unreliable, 0);
        auto ev = run({host.get()}, host.get(), [](const auto&) { return true; }, 1);
        hev.insert(hev.end(), ev.begin(), ev.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(9));
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    const auto it = std::find_if(hev.begin(), hev.end(), [](const auto& x) { return x.kind == TransportEvent::Kind::Disconnected; });
    std::printf("       udp: vanished peer reported after %lld ms despite injected traffic (%s)\n", static_cast<long long>(ms),
                it == hev.end() ? "never" : std::string(it->reason).c_str());
    CHECK(it != hev.end() && it->reason == "idle_timeout" && ms < 2500);
}

// Review #9 FB-9-05: an invitation grants nothing by itself; misused invitations never authenticate.
TEST(secure_invitation_misuse_is_refused) {
    for (auto k : backends()) {
        Key host_key, player, stranger, fake_host;
        const auto inv = invite("session-1");
        auto hcfg = *secure(host_key, {player.spki()}, inv);
        auto host = open_secure(k, hcfg, true);
        const Endpoint ep = Endpoint::loopback(host->local_endpoint().port);
        struct Case {
            const char* name;
            SecureTransportConfig cfg;
        };
        auto c_inv_other = *secure(player, {host_key.spki()}, invite("session-2"));        // conflicting invitation
        auto c_stranger = *secure(stranger, {host_key.spki()}, inv);                         // valid invitation, not allowed
        auto c_spoofed = *secure(player, {fake_host.spki()}, inv);                           // invitation naming another host key
        auto c_swapped = *secure(stranger, {host_key.spki()}, inv);                          // presents the player's invitation
        c_swapped.pinned_peers = {host_key.spki()};
        auto c_stale = *secure(player, {host_key.spki()}, inv);                              // its clock says: expired
        c_stale.unix_clock = [] { return static_cast<std::uint64_t>(std::time(nullptr)) + 7200; };
        std::vector<Case> cases{{"conflicting invitation", c_inv_other}, {"invitation alone (not allowed)", c_stranger},
                                {"spoofed host key", c_spoofed}, {"swapped identity", c_swapped}, {"stale invitation", c_stale}};
        for (auto& cs : cases) {
            auto c = open_secure(k, cs.cfg, false);
            c->connect(ep);
            const auto ev = run({host.get(), c.get()}, c.get(), [](const auto& e) {
                return std::any_of(e.begin(), e.end(), [](const auto& x) { return x.kind != TransportEvent::Kind::Message; });
            }, 15000);
            const bool connected = count(ev, TransportEvent::Kind::Connected) > 0;
            std::printf("       %s: %s -> %s\n", to_string(k), cs.name, connected ? "CONNECTED" : "refused");
            CHECK(!connected);
        }
        CHECK(secure_stats(*host)->authenticated == 0);
    }
}

// Review #9 answer 1: a flood of unauthenticated ClientHellos is rate-limited and counted, and the established
// player stays responsive.
TEST(secure_client_hello_flood_is_rate_limited_and_player_stays_responsive) {
    Key host_key, player, stranger;
    auto hcfg = *secure(host_key, {player.spki()}, invite());
    hcfg.max_new_handshakes_per_s = 16;
    auto host = open_secure(TransportKind::Udp, hcfg, true);
    auto c = open_secure(TransportKind::Udp, *secure(player, {host_key.spki()}, hcfg.invitation), false);
    const Endpoint ep = Endpoint::loopback(host->local_endpoint().port);
    const auto to_host = c->connect(ep).value();
    run({host.get(), c.get()}, host.get(), [](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; }, 5000);
    std::string err;
    std::vector<std::unique_ptr<Transport>> flood;
    for (int i = 0; i < 60; ++i) {
        flood.push_back(make_transport(TransportKind::Udp, {}, &err));
        CHECK(flood.back()->open(Endpoint::loopback(0), false, &err));
        const auto p = flood.back()->connect(ep).value();
        for (int j = 0; j < 5; ++j) flood.back()->send(p, forged_client_hello(stranger, host_key.spki(), hcfg.invitation), Delivery::Unreliable, 0);
    }
    std::uint32_t got = 0;
    for (std::uint32_t i = 1; i <= 20; ++i) CHECK(c->send(to_host, tagged(9, i), Delivery::ReliableOrdered, 0) == SendResult::Sent);
    run({host.get(), c.get()}, host.get(), [&](const auto& e) {
        got = static_cast<std::uint32_t>(count(e, TransportEvent::Kind::Message));
        return got == 20;
    }, 5000);
    const auto* hs = secure_stats(*host);
    std::printf("       udp: 300 forged ClientHellos -> opened %llu, rate-limited %llu, refused by cap %llu; player delivered %u/20\n",
                static_cast<unsigned long long>(hs->pending_opened), static_cast<unsigned long long>(hs->hello_rate_limited),
                static_cast<unsigned long long>(hs->pending_refused_cap), got);
    CHECK(hs->hello_rate_limited >= 200);  // 300 attempts, 16 per second admitted to the cap check
    CHECK(hs->pending_opened <= 1 + 4);       // the player + at most max_pending_total strangers
    CHECK(got == 20);
}

// Review #9 FB-9-02: application-level events over the secure transport keep exactly-once order across a
// re-authentication (outage -> new DTLS handshake -> game reconnect with the token -> host replay).
TEST(secure_events_keep_exactly_once_order_across_re_authentication) {
    for (auto k : backends()) {
        World w(k);
        auto& c = w.add(&w.k1, {w.host_key.spki()});
        CHECK(w.run_until([&] { return c.state() == ClientState::Connected; }, 10 * kSec));
        const auto player = c.player_id();
        std::vector<std::uint8_t> applied;
        auto drain = [&](std::uint32_t apply_up_to) {
            for (const auto& e : c.take_events()) applied.push_back(e.payload[0]);
            if (apply_up_to) c.ack_events_applied(apply_up_to, w.clk.now);
        };
        for (std::uint8_t i = 1; i <= 10; ++i) CHECK(w.host->send_event(player, 1, std::vector<std::uint8_t>{i}, w.clk.now));
        CHECK(w.run_until([&] { drain(0); return applied.size() == 10; }, 5 * kSec));
        c.ack_events_applied(6, w.clk.now);  // 7..10 delivered but not applied yet
        ImpairmentConfig cut;
        cut.loss = 1.0;
        CHECK(c.set_impairment(cut));
        CHECK(w.host->set_impairment(cut));
        CHECK(w.run_until([&] { return w.host->connected_count() == 0; }, 8 * kSec));
        for (std::uint8_t i = 11; i <= 15; ++i) CHECK(w.host->send_event(player, 1, std::vector<std::uint8_t>{i}, w.clk.now));
        CHECK(c.set_impairment({}));
        CHECK(w.host->set_impairment({}));
        CHECK(w.run_until([&] { return c.state() == ClientState::Connected && c.stats().reconnects >= 1; }, 15 * kSec));
        CHECK(c.player_id() == player);
        CHECK(w.run_until([&] { drain(0); return applied.size() == 15; }, 5 * kSec));
        c.ack_events_applied(15, w.clk.now);
        CHECK(w.run_until([&] { return w.host->unapplied_events(player) == 0; }, 5 * kSec));
        for (std::size_t i = 0; i < applied.size(); ++i) CHECK(applied[i] == i + 1);
        const auto* hs = secure_stats(*w.host->transport_for_test());
        std::printf("       %s: 15 events exactly once in order across a re-authentication (DTLS sessions %llu, replayed %llu)\n",
                    to_string(k), static_cast<unsigned long long>(hs->authenticated),
                    static_cast<unsigned long long>(w.host->stats().events_replayed));
        CHECK(hs->authenticated >= 2);
    }
}

// EXP-022: a clean leave after a re-authentication must reach the host as a clean leave (no timeout).
TEST(secure_clean_leave_after_reconnect_is_not_a_timeout) {
    for (auto k : backends()) {
        for (bool with_reconnect : {false, true}) {
            World w(k);
            auto& c = w.add(&w.k1, {w.host_key.spki()});
            CHECK(w.run_until([&] { return c.state() == ClientState::Connected; }, 10 * kSec));
            if (with_reconnect) {
                ImpairmentConfig cut;
                cut.loss = 1.0;
                CHECK(c.set_impairment(cut));
                CHECK(w.host->set_impairment(cut));
                CHECK(w.run_until([&] { return w.host->connected_count() == 0; }, 8 * kSec));
                CHECK(c.set_impairment({}));
                CHECK(w.host->set_impairment({}));
                CHECK(w.run_until([&] { return c.state() == ClientState::Connected && c.stats().reconnects >= 1; }, 15 * kSec));
                w.run_until([] { return false; }, kSec);
            }
            const auto timeouts_before = w.host->stats().timeouts;
            c.disconnect();
            // The client process exits: close() drains for a bounded time while the host keeps running in its own
            // process. One thread here, so pump the host from a helper thread during the drain.
            std::atomic<bool> stop{false};
            std::thread hostloop([&] {
                while (!stop) {
                    w.host->update(w.clk.now += 5'000);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });
            c.close();
            stop = true;
            hostloop.join();
            CHECK(w.run_until([&] { return w.host->slot_count() == 0; }, 2 * kSec));
            std::printf("       %s, %s: host slots after leave %zu, extra timeouts %llu\n", to_string(k),
                        with_reconnect ? "after reconnect" : "no reconnect", w.host->slot_count(),
                        static_cast<unsigned long long>(w.host->stats().timeouts - timeouts_before));
            CHECK(w.host->stats().timeouts == timeouts_before);
        }
    }
}

// Review #10 FB-10-02: when the transport refuses an event (here the secure channel's window is full because the
// client stopped acknowledging), the host does not count it as sent: it retries with back-off, and every event still
// arrives exactly once and in order once the client is back.
TEST(secure_refused_event_sends_are_retried_not_counted_as_sent) {
    host::HostConfig hc;
    hc.max_unacked_events = 400;  // more than the secure channel's 256-message window
    World w(TransportKind::Udp, true, hc);
    auto& c = w.add(&w.k1, {w.host_key.spki()});
    CHECK(w.run_until([&] { return c.state() == ClientState::Connected; }, 10 * kSec));
    const auto player = c.player_id();
    std::uint32_t queued = 0;
    for (std::uint32_t i = 1; i <= 300; ++i) {
        const std::vector<std::uint8_t> p{static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i >> 8)};
        queued += w.host->send_event(player, 1, p, w.clk.now) ? 1 : 0;
    }
    // Freeze the client (no polling: no DTLS acks) while the host keeps sending snapshots and resending events.
    for (int i = 0; i < 400; ++i) {
        w.host->update(w.clk.now += 5'000);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto refused = w.host->stats().events_send_refused;
    std::vector<std::uint32_t> applied;
    CHECK(w.run_until([&] {
        for (const auto& e : c.take_events()) {
            applied.push_back(e.payload[0] | (std::uint32_t{e.payload[1]} << 8));
            c.ack_events_applied(e.id, w.clk.now);
        }
        return applied.size() == queued && w.host->unapplied_events(player) == 0;
    }, 20 * kSec));
    std::printf("       udp: %u events; host sends refused by the transport while the client was frozen: %llu; all applied once, in order\n",
                queued, static_cast<unsigned long long>(refused));
    CHECK(refused >= 1);
    for (std::size_t i = 0; i < applied.size(); ++i) CHECK(applied[i] == i + 1);
    // Independent review: a refused event must stop the slot's sends, or later ones overtake it (client gaps).
    std::printf("       udp: client saw %llu events out of order, %llu duplicates\n",
                static_cast<unsigned long long>(c.stats().events_out_of_order), static_cast<unsigned long long>(c.stats().events_duplicate));
    CHECK(c.stats().events_out_of_order == 0);
}
