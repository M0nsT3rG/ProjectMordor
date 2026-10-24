// Secure transport decorator (review #7 answer 3, FB-7-01, FB-7-05): Host/Client game traffic through DTLS with
// pinned keys and the invitation binding, on every backend, plus the bounded pre-authentication lifecycle.
#include <openssl/evp.h>

#include <chrono>
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
    explicit World(TransportKind k, bool secure_host = true) : kind(k) {
        host::HostConfig hc;
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
        CHECK(c->send(to_host, std::vector<std::uint8_t>{7}, Delivery::ReliableOrdered, 0) == SendResult::Unsupported);
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
