// Network tests: loopback UDP with deterministic impairment (latency, jitter, loss, duplication).
#include <array>

#include "test_support.hpp"

using namespace mordor;
using namespace mordor::net;
using client::ClientState;

namespace {
constexpr std::uint64_t kSec = 1'000'000;

ImpairmentConfig impair(double loss, std::uint32_t latency_ms, std::uint32_t jitter_ms, std::uint64_t seed,
                        double duplicate = 0.0) {
    ImpairmentConfig c;
    c.loss = loss;
    c.latency_ms = latency_ms;
    c.jitter_ms = jitter_ms;
    c.seed = seed;
    c.duplicate = duplicate;
    return c;
}

std::uint64_t count_received(UdpSocket& s) {
    std::array<std::uint8_t, 2048> buf{};
    std::uint64_t n = 0;
    Endpoint from;
    for (int idle = 0; idle < 2000;) {
        if (s.recv_from(from, buf).status == RecvStatus::Data) {
            ++n;
            idle = 0;
        } else {
            ++idle;
            std::this_thread::yield();
        }
    }
    return n;
}

// Moves the client for `move_us`, stops, settles, then checks prediction and the other player's view agree with the host.
void move_and_check_convergence(test::Session& s, client::Client& mover, client::Client& watcher, std::uint64_t move_us,
                                double eps) {
    mover.set_move(0.6f, -0.8f);
    s.run(move_us);
    mover.set_move(0, 0);
    s.run(3 * kSec);
    const auto auth = s.host.player_transform(mover.player_id());
    CHECK(auth.has_value());
    CHECK(std::hypot(auth->x, auth->z) > 1.0);  // it really moved
    CHECK_NEAR(mover.predicted_self()->x, auth->x, eps);
    CHECK_NEAR(mover.predicted_self()->z, auth->z, eps);
    const auto view = watcher.remote_view(mover.entity_id(), s.clk.now);
    CHECK(view.has_value());
    CHECK_NEAR(view->x, auth->x, eps);
    CHECK_NEAR(view->z, auth->z, eps);
}
}  // namespace

TEST(impairment_loss_is_deterministic_and_close_to_configured_rate) {
    UDP_ONLY("tests ImpairedSender, the UDP backend's impairment");
    std::uint64_t results[2];
    for (int run = 0; run < 2; ++run) {
        UdpSocket rx, tx;
        CHECK(rx.open(Endpoint::loopback(0)));
        CHECK(tx.open(Endpoint::loopback(0)));
        ImpairedSender sender(tx, impair(0.3, 0, 0, 42));
        for (int i = 0; i < 2000; ++i) sender.send(rx.local_endpoint(), {1, 2, 3}, 0);
        results[run] = sender.stats().dropped;
        CHECK(count_received(rx) + sender.stats().send_failures == 2000 - sender.stats().dropped);
    }
    CHECK(results[0] == results[1]);  // same seed, same drops
    CHECK(results[0] > 500 && results[0] < 700);
}

TEST(impairment_delays_until_due) {
    UDP_ONLY("tests ImpairedSender, the UDP backend's impairment");
    UdpSocket rx, tx;
    CHECK(rx.open(Endpoint::loopback(0)));
    CHECK(tx.open(Endpoint::loopback(0)));
    ImpairedSender sender(tx, impair(0, 100, 0, 1));
    sender.send(rx.local_endpoint(), {9}, 1'000'000);
    CHECK(sender.stats().sent == 0);
    sender.flush(1'099'999);
    CHECK(sender.stats().sent == 0);
    sender.flush(1'100'000);
    CHECK(sender.stats().sent == 1);
    CHECK(count_received(rx) == 1);
}

// Latency measurement: 50 ms each way should give roughly 100 ms RTT.
TEST(latency_is_measured) {
    host::HostConfig hc;
    hc.impairment = impair(0, 50, 0, 3);
    test::Session s(hc);
    client::ClientConfig cc;
    cc.impairment = impair(0, 50, 0, 4);
    auto& c = s.add_client(cc);
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 3 * kSec));
    s.run(3 * kSec);
    CHECK(c.rtt_ms().has_value());
    CHECK(*c.rtt_ms() >= 95.0 && *c.rtt_ms() <= 120.0);
}

// Test 7: delayed and reordered packets.
TEST(delayed_and_reordered_packets_converge) {
    host::HostConfig hc;
    hc.impairment = impair(0, 80, 60, 11);
    test::Session s(hc);
    client::ClientConfig cc;
    cc.impairment = impair(0, 80, 60, 12);
    auto& a = s.add_client(cc);
    cc.impairment.seed = 13;
    auto& b = s.add_client(cc);
    CHECK(s.pump_until([&] { return s.all_connected() && a.remote_entities().size() == 1; }, 5 * kSec));
    move_and_check_convergence(s, a, b, 2 * kSec, 1e-3);
    CHECK(a.stats().stale_snapshots > 0);  // jitter reordered snapshots and the client dropped the old ones
    CHECK(s.all_connected());
}

// Test 8: sustained loss keeps the session alive and the state converges.
TEST(packet_loss_does_not_break_session) {
    host::HostConfig hc;
    hc.impairment = impair(0.25, 20, 10, 21);
    test::Session s(hc);
    client::ClientConfig cc;
    cc.impairment = impair(0.25, 20, 10, 22);
    auto& a = s.add_client(cc);
    cc.impairment.seed = 23;
    auto& b = s.add_client(cc);
    CHECK(s.pump_until([&] { return s.all_connected() && a.remote_entities().size() == 1; }, 5 * kSec));
    move_and_check_convergence(s, a, b, 3 * kSec, 1e-3);
    CHECK(s.all_connected());
    CHECK(s.host.stats().timeouts == 0);
    CHECK(s.host.stats().invalid_packets == 0);
}

TEST(heavy_loss_and_duplication_never_crash) {
    host::HostConfig hc;
    hc.impairment = impair(0.6, 30, 30, 31, 0.3);
    test::Session s(hc);
    client::ClientConfig cc;
    cc.impairment = impair(0.6, 30, 30, 32, 0.3);
    auto& a = s.add_client(cc);
    a.set_move(1, 1);
    s.run(6 * kSec);
    CHECK(s.host.running());
    const auto st = a.state();
    CHECK(st == ClientState::Connected || st == ClientState::Connecting || st == ClientState::Reconnecting);
    CHECK(s.host.stats().invalid_packets == 0);  // duplicates are valid packets, handled as stale
}

// Test 9: a client that loses connectivity times out on both ends, then recovers its identity.
TEST(timed_out_client_reconnects_with_same_identity) {
    UDP_ONLY("asymmetric between clients (b keeps reaching the host while a is cut); GNS fake loss is process-global and cuts both (see symmetric_outage_recovers_fast_with_same_identity)");
    test::Session s;
    auto& a = s.add_client();
    auto& b = s.add_client();
    CHECK(s.pump_until([&] { return s.all_connected(); }, 2 * kSec));
    a.set_move(1, 0);
    s.run(kSec / 2);
    a.set_move(0, 0);
    const auto player = a.player_id(), entity = a.entity_id();
    const auto blackout = impair(1.0, 0, 0, 5);
    a.set_impairment(blackout);  // a's packets vanish...
    // The host's replies are dropped too. b still reaches the host, so the host keeps b connected,
    // but b hears nothing and also enters Reconnecting on its own side.
    s.host.set_impairment(blackout);
    CHECK(s.pump_until([&] { return a.state() == ClientState::Reconnecting && s.host.connected_count() == 1; }, 5 * kSec));
    CHECK(s.host.stats().timeouts == 1);
    CHECK(s.host.world().entities().size() == 2);  // entities kept during the grace period
    a.set_impairment({});
    s.host.set_impairment({});
    CHECK(s.pump_until([&] { return s.all_connected() && a.predicted_self(); }, 5 * kSec));
    CHECK(a.player_id() == player && a.entity_id() == entity);
    CHECK(a.stats().reconnects == 1);
    CHECK(s.host.stats().reconnects == 1);  // only a had timed out on the host side
    CHECK(b.stats().reconnects == 1);       // b recovered via its token while its host slot stayed live
    CHECK(s.host.world().entities().size() == 2);
    CHECK_NEAR(a.predicted_self()->x, s.host.player_transform(player)->x, 1e-3);
}

TEST(reconnect_gives_up_after_window) {
    test::Session s;
    client::ClientConfig cc;
    cc.reconnect_window_us = 2 * kSec;
    auto& a = s.add_client(cc);
    CHECK(s.pump_until([&] { return a.state() == ClientState::Connected; }, 2 * kSec));
    s.host.set_impairment(impair(1.0, 0, 0, 1));
    s.host.shutdown();  // the shutdown notice is dropped, so the client only sees silence (host crash)
    CHECK(test::pump(s.clk, nullptr, s.ptrs(), [&] { return a.state() == ClientState::Reconnecting; }, 5 * kSec));
    CHECK(test::pump(s.clk, nullptr, s.ptrs(), [&] { return a.state() == ClientState::Failed; }, 4 * kSec));
}

TEST(silent_host_loss_ends_in_failed_state) {
    UDP_ONLY("uses a raw UDP socket as a fake peer; a GNS host/client only talks GNS");
    test::SimClock clk;
    UdpSocket silent;  // a "host" that never answers
    CHECK(silent.open(Endpoint::loopback(0)));
    client::ClientConfig cc;
    cc.host = silent.local_endpoint();
    cc.connect_timeout_us = 2 * kSec;
    client::Client c(cc, test::test_logger("client"));
    CHECK(c.start());
    c.connect(clk.now);
    CHECK(test::pump(clk, nullptr, {&c}, [&] { return c.state() == ClientState::Failed; }, 4 * kSec));
}

// ---- Connection lifecycle and timeout ownership, every backend (review #5 FB-5-02). ----
namespace {
std::uint16_t unused_port() {  // a loopback port nobody listens on (probed, then released)
    UdpSocket probe;
    if (!probe.open(Endpoint::loopback(0))) throw test::CheckFailure("probe socket failed");
    const auto p = probe.local_endpoint().port;
    probe.close();
    return p;
}
void report(const char* test, const client::Client& c, std::uint64_t elapsed_us) {
    std::printf("       %s on %s: %s (%s) after %llu ms\n", test, to_string(test::test_transport()), to_string(c.state()),
                c.state_reason().c_str(), static_cast<unsigned long long>(elapsed_us / 1000));
}
}  // namespace

TEST(connect_to_absent_host_fails_within_deadline) {
    test::SimClock clk;
    client::ClientConfig cc;
    cc.host = Endpoint::loopback(unused_port());
    cc.transport = test::test_transport();
    client::Client c(cc, test::test_logger("client"));
    CHECK(c.start());
    const auto t0 = clk.now;
    c.connect(clk.now);
    CHECK(test::pump(clk, nullptr, {&c}, [&] { return c.state() == ClientState::Failed; }, cc.connect_deadline_us + kSec));
    report("connect_to_absent_host_fails_within_deadline", c, clk.now - t0);
    CHECK(clk.now - t0 <= cc.connect_deadline_us + 50'000);  // never beyond the overall bound (one step of slack)
}

// The overall deadline holds even when the transport's own handshake timeout is longer (GNS: 10 s) and the
// session window has not started: nothing ever answers, and nothing reports a failure.
TEST(black_holed_connect_hits_overall_deadline) {
    test::Session s;
    client::ClientConfig cc;
    cc.connect_deadline_us = 3 * kSec;  // shorter than both connect_timeout_us (5 s) and GNS TimeoutInitial (10 s)
    cc.impairment = impair(1.0, 0, 0, 3);
    const auto t0 = s.clk.now;
    auto& c = s.add_client(cc);
    CHECK(s.pump_until([&] { return c.state() == ClientState::Failed; }, 5 * kSec));
    report("black_holed_connect_hits_overall_deadline", c, s.clk.now - t0);
    CHECK(c.state_reason() == "connect_deadline");
    CHECK(s.clk.now - t0 >= 3 * kSec && s.clk.now - t0 <= 3 * kSec + 50'000);
    CHECK(s.host.connected_count() == 0);
    c.set_impairment({});
}

// The host goes away while the client is still connecting: the client must end in Failed within its bound,
// never in Connected, on every backend (UDP: session timers; GNS: transport close during the handshake).
TEST(host_gone_during_handshake_fails_within_deadline) {
    test::Session s;
    auto& c = s.add_client();
    const auto t0 = s.clk.now;
    s.host.shutdown();  // before the host ever processed the request: it can never have accepted
    bool ever_connected = false;
    CHECK(test::pump(s.clk, nullptr, s.ptrs(), [&] {
        ever_connected = ever_connected || c.state() == ClientState::Connected;
        return c.state() == ClientState::Failed || c.state() == ClientState::Disconnected;
    }, c.config().connect_deadline_us + kSec));
    report("host_gone_during_handshake_fails_within_deadline", c, s.clk.now - t0);
    CHECK(!ever_connected);
    CHECK(s.clk.now - t0 <= c.config().connect_deadline_us + 50'000);
}

// Repeated outages: each one ends in a reconnect with the same identity, and each recovery stays bounded.
TEST(repeated_outages_reconnect_each_time_with_same_identity) {
    test::Session s;
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 3 * kSec));
    const auto player = c.player_id();
    for (std::uint64_t round = 1; round <= 3; ++round) {
        const auto cut = impair(1.0, 0, 0, 70 + round);
        CHECK(c.set_impairment(cut));
        CHECK(s.host.set_impairment(cut));  // same config: allowed even where impairment is process-global
        CHECK(s.pump_until([&] { return s.host.connected_count() == 0; }, 6 * kSec));
        CHECK(c.set_impairment({}));
        CHECK(s.host.set_impairment({}));
        const auto restored = s.clk.now;
        CHECK(s.pump_until([&] { return c.state() == ClientState::Connected && c.stats().reconnects == round; }, 6 * kSec));
        CHECK(s.clk.now - restored < 4 * kSec);
        CHECK(c.player_id() == player);
    }
    CHECK(s.host.stats().joins == 1);
}

TEST(expired_grace_period_removes_entity) {
    host::HostConfig hc;
    hc.reconnect_grace_us = 1 * kSec;
    test::Session s(hc);
    auto& a = s.add_client();
    CHECK(s.pump_until([&] { return a.state() == ClientState::Connected; }, 2 * kSec));
    a.set_impairment(impair(1.0, 0, 0, 1));
    CHECK(s.pump_until([&] { return s.host.slot_count() == 0; }, 8 * kSec));
    CHECK(s.host.world().entities().empty());
}

// FB-1-01 under real jitter: the host never moves an entity faster than one timestep per server
// tick, and the predicting client still converges to the authority after it stops.
TEST(jitter_never_exceeds_tick_movement_budget) {
    host::HostConfig hc;
    hc.impairment = impair(0, 40, 80, 41);
    test::Session s(hc);
    client::ClientConfig cc;
    cc.impairment = impair(0, 40, 80, 42);
    auto& a = s.add_client(cc);
    CHECK(s.pump_until([&] { return a.state() == ClientState::Connected && a.predicted_self(); }, 5 * kSec));
    const float step = sim::kMoveSpeed / 30.0f;
    auto last = *s.host.player_transform(a.player_id());
    auto last_tick = s.host.world().tick();
    float worst_ratio = 0;
    a.set_move(1.0f, 0.0f);
    const auto end = s.clk.now + 4 * kSec;
    while (s.clk.now < end) {
        s.host.update(s.clk.now);
        a.update(s.clk.now);
        const auto t = *s.host.player_transform(a.player_id());
        const auto ticks = s.host.world().tick() - last_tick;
        if (ticks > 0) {
            const float moved = std::hypot(t.x - last.x, t.z - last.z);
            worst_ratio = std::max(worst_ratio, moved / (step * static_cast<float>(ticks)));
            last = t;
            last_tick = s.host.world().tick();
        }
        s.clk.now += 5'000;
        test::step_wait(5'000);
    }
    CHECK(worst_ratio <= 1.0f + 1e-3f);
    CHECK(s.host.player_transform(a.player_id())->x > 5.0f);  // it really moved
    a.set_move(0, 0);
    s.run(3 * kSec);
    CHECK_NEAR(a.predicted_self()->x, s.host.player_transform(a.player_id())->x, 1e-3);
    CHECK(s.all_connected());
}

// Review #2 FB-2-02, smoke test with a real host and heavy host-side jitter: recovery must happen exactly
// once and stay stable. NOTE: in practice this run delivered no late notice (stale_notices was 0), so it
// does not prove the stale-notice guard; the deterministic proof is the fake-host tests in the
// integration suite (delayed_timeout_notice_after_reconnect_is_ignored and friends).
TEST(timeout_notice_recovery_under_jitter_reconnects_once) {
    // Its earlier GNS "pass" was false: the client's cut silently replaced the host's jitter process-wide, so
    // the scenario never ran there. The single-owner guard (review #5 FB-5-03) now refuses that overwrite.
    UDP_ONLY("asymmetric (host jitter + client-only uplink cut); GNS fake impairment is process-global");
    host::HostConfig hc;
    hc.impairment = impair(0, 20, 400, 61);
    test::Session s(hc);
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 5 * kSec));
    const auto player = c.player_id();
    c.set_impairment(impair(1.0, 0, 0, 62));  // uplink cut until the host times the client out
    CHECK(s.pump_until([&] { return s.host.connected_count() == 0; }, 6 * kSec));
    c.set_impairment(impair(0, 20, 0, 63));
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected && c.stats().reconnects == 1; }, 5 * kSec));
    s.run(3 * kSec);  // any late notices arrive during this window
    CHECK(c.state() == ClientState::Connected);
    CHECK(c.stats().reconnects == 1);
    CHECK(c.player_id() == player);
    CHECK(s.host.connected_count() == 1);
}

// Review #9 FB-9-02: events stay exactly-once and in order under loss, duplication and jitter (unreliable path:
// host resend + client dedup; reliable backends: transport retransmission + replay).
TEST(events_are_exactly_once_and_in_order_under_impairment) {
    host::HostConfig hc;
    hc.impairment = impair(0.2, 20, 30, 81, 0.1);
    test::Session s(hc);
    client::ClientConfig cc;
    cc.impairment = impair(0.2, 20, 30, 82, 0.1);
    auto& c = s.add_client(cc);
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 10 * kSec));
    for (std::uint32_t i = 1; i <= 40; ++i) {
        std::vector<std::uint8_t> p{static_cast<std::uint8_t>(i)};
        CHECK(s.host.send_event(c.player_id(), 3, p, s.clk.now));
    }
    std::vector<std::uint8_t> applied;
    CHECK(s.pump_until([&] {
        for (const auto& e : c.take_events()) {
            applied.push_back(e.payload[0]);
            c.ack_events_applied(e.id, s.clk.now);
        }
        return applied.size() == 40 && s.host.unapplied_events(c.player_id()) == 0;
    }, 20 * kSec));
    for (std::size_t i = 0; i < applied.size(); ++i) CHECK(applied[i] == i + 1);
    std::printf("       %s: 40/40 events applied in order; host retransmits %llu, client duplicates dropped %llu\n",
                to_string(test::test_transport()), static_cast<unsigned long long>(s.host.stats().events_retransmitted),
                static_cast<unsigned long long>(c.stats().events_duplicate));
}
