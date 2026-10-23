// Integration tests: real UDP sockets on 127.0.0.1, no impairment.
#include <array>

#include "test_support.hpp"

using namespace mordor;
using namespace mordor::net;
using client::ClientState;

namespace {
constexpr std::uint64_t kSec = 1'000'000;

std::optional<Packet> recv_packet(UdpSocket& s, Endpoint& from) {
    std::array<std::uint8_t, 2048> buf{};
    for (int i = 0; i < 2000; ++i) {
        const auto r = s.recv_from(from, buf);
        if (r.status == RecvStatus::Data) {
            auto d = decode({buf.data(), r.size});
            if (d.packet) return d.packet;
        }
        std::this_thread::yield();
    }
    return std::nullopt;
}
}  // namespace

// Test 1
TEST(host_starts) {
    test::Session s;
    CHECK(s.host.running());
    CHECK(s.host.port() != 0);
    CHECK(s.host.session_id() != 0);
    CHECK(s.host.connected_count() == 0);
}

TEST(host_rejects_invalid_config) {
    host::HostConfig hc;
    hc.max_players = 3;  // above protocol limit
    host::Host h(hc, test::test_logger("host"));
    std::string err;
    CHECK(!h.start(&err));
    CHECK(!err.empty());
}

// Test 2
TEST(client_connects) {
    test::Session s;
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected && c.latest_server_tick(); }, 2 * kSec));
    CHECK(c.player_id() != 0);
    CHECK(c.session_id() == s.host.session_id());
    CHECK(s.host.connected_count() == 1);
    CHECK(c.predicted_self().has_value());
}

// Test 3
TEST(two_clients_get_unique_identities) {
    test::Session s;
    auto& a = s.add_client();
    auto& b = s.add_client();
    CHECK(s.pump_until([&] { return s.all_connected() && a.remote_entities().size() == 1 && b.remote_entities().size() == 1; },
                       2 * kSec));
    CHECK(a.player_id() != b.player_id());
    CHECK(a.entity_id() != b.entity_id());
    CHECK(a.remote_entities()[0] == b.entity_id());
    CHECK(b.remote_entities()[0] == a.entity_id());
    CHECK(s.host.world().entities().size() == 2);
}

TEST(third_client_is_rejected_when_full) {
    test::Session s;
    s.add_client();
    s.add_client();
    CHECK(s.pump_until([&] { return s.all_connected(); }, 2 * kSec));
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Rejected; }, 2 * kSec));
    CHECK(c.reject_reason() == RejectReason::ServerFull);
    CHECK(s.host.connected_count() == 2);
}

// Test 4 (integration part): movement by one player replicates to the other.
TEST(transform_replicates_between_clients) {
    test::Session s;
    auto& mover = s.add_client();
    auto& watcher = s.add_client();
    CHECK(s.pump_until([&] { return s.all_connected(); }, 2 * kSec));
    mover.set_move(1.0f, 0.0f);
    s.run(1 * kSec);
    mover.set_move(0.0f, 0.0f);
    s.run(1 * kSec);  // let everything settle
    const auto host_t = s.host.player_transform(mover.player_id());
    CHECK(host_t.has_value());
    CHECK(host_t->x > 3.0f);  // moved roughly kMoveSpeed * 1 s from its spawn
    const auto seen = watcher.remote_view(mover.entity_id(), s.clk.now);
    CHECK(seen.has_value());
    CHECK_NEAR(seen->x, host_t->x, 1e-4);
    CHECK_NEAR(seen->z, host_t->z, 1e-4);
    CHECK_NEAR(mover.predicted_self()->x, host_t->x, 1e-3);  // prediction reconciled with authority
}

TEST(prediction_moves_immediately_and_matches_authority) {
    host::HostConfig hc;
    hc.impairment.latency_ms = 100;  // 200 ms RTT: authority cannot reflect the move for several ticks
    test::Session s(hc);
    client::ClientConfig cc;
    cc.impairment.latency_ms = 100;
    auto& c = s.add_client(cc);
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected && c.predicted_self(); }, 3 * kSec));
    const float x0 = c.predicted_self()->x;
    const float auth0 = c.authoritative_self()->x;
    c.set_move(1.0f, 0.0f);
    s.pump_until([&] { return c.predicted_self()->x > x0; }, kSec / 10);
    CHECK(c.predicted_self()->x > x0);         // moved locally...
    CHECK(c.authoritative_self()->x == auth0);  // ...before the host could have applied the input
    s.run(kSec);
    s.run(kSec / 2);
    c.set_move(0.0f, 0.0f);
    s.run(kSec / 2);
    CHECK_NEAR(c.predicted_self()->x, s.host.player_transform(c.player_id())->x, 1e-3);
    CHECK_NEAR(c.authoritative_self()->x, c.predicted_self()->x, 1e-3);
}

// Test 5 (integration part): a fake host sends stale and duplicate snapshots; the client ignores them.
TEST(client_rejects_stale_snapshots) {
    UdpSocket fake;
    CHECK(fake.open(Endpoint::loopback(0)));
    client::ClientConfig cc;
    cc.host = fake.local_endpoint();
    client::Client c(cc, test::test_logger("client"));
    CHECK(c.start());
    test::SimClock clk;
    c.connect(clk.now);
    c.update(clk.now);
    Endpoint client_ep;
    auto req = recv_packet(fake, client_ep);
    CHECK(req && std::holds_alternative<ConnectRequest>(req->message));
    Header h;
    h.session = 77;
    auto send = [&](const Message& m) {
        ++h.sequence;
        CHECK(fake.send_to(client_ep, encode(h, m)));
        for (int i = 0; i < 50; ++i) {
            c.update(clk.now);
            std::this_thread::yield();
        }
    };
    send(ConnectAccept{0xDEAD, 1, 1, 5, 30, 0});  // wrong nonce: must be ignored
    CHECK(c.state() == ClientState::Connecting);
    send(ConnectAccept{std::get<ConnectRequest>(req->message).client_nonce, 1, 1, 5, 30, 0});
    CHECK(c.state() == ClientState::Connected);
    Snapshot snap;
    snap.entities.push_back({1, 1, {10, 0, 0, 0}, 0, 0});
    snap.entities.push_back({2, 2, {20, 0, 0, 0}, 0, 0});
    snap.server_tick = 10;
    send(snap);
    CHECK(c.stats().snapshots_applied == 1);
    snap.server_tick = 9;
    snap.entities[0].transform.x = -50;
    send(snap);  // older
    snap.server_tick = 10;
    send(snap);  // duplicate
    CHECK(c.stats().snapshots_applied == 1);
    CHECK(c.stats().stale_snapshots == 2);
    CHECK(c.authoritative_self()->x == 10.0f);
    h.session = 78;  // wrong session is ignored entirely
    snap.server_tick = 11;
    send(snap);
    CHECK(c.stats().snapshots_applied == 1);
}

// Test 10
TEST(protocol_version_mismatch_is_rejected) {
    test::Session s;
    client::ClientConfig cc;
    cc.protocol_version = kProtocolVersion + 1;
    auto& c = s.add_client(cc);
    CHECK(s.pump_until([&] { return c.state() == ClientState::Rejected; }, 2 * kSec));
    CHECK(c.reject_reason() == RejectReason::VersionMismatch);
    CHECK(s.host.stats().rejected_version >= 1);
    CHECK(s.host.connected_count() == 0);
    CHECK(s.host.world().entities().empty());
}

// Test 11 (integration part): garbage over the wire is counted and dropped; the host keeps serving.
TEST(host_survives_invalid_datagrams) {
    test::Session s;
    UdpSocket attacker;
    CHECK(attacker.open(Endpoint::loopback(0)));
    const auto host_ep = Endpoint::loopback(s.host.port());
    std::mt19937 rng(99);
    std::vector<std::vector<std::uint8_t>> junk = {{}, {1, 2, 3}, std::vector<std::uint8_t>(1500, 0xAB),
                                                   std::vector<std::uint8_t>(4000, 0xCD)};
    auto valid = encode(Header{}, ConnectRequest{1, 0});
    valid.push_back(0);  // length mismatch
    junk.push_back(valid);
    for (int i = 0; i < 200; ++i) {
        std::vector<std::uint8_t> r(rng() % 100);
        for (auto& b : r) b = static_cast<std::uint8_t>(rng());
        junk.push_back(r);
    }
    // A well-formed Input packet from an unknown peer must be ignored, not crash or create a player.
    InputBatch b;
    b.inputs.push_back({1, 1, 0});
    junk.push_back(encode(Header{}, b));
    for (const auto& j : junk) attacker.send_to(host_ep, j);
    s.run(kSec / 2);
    CHECK(s.host.running());
    CHECK(s.host.stats().invalid_packets >= 200);
    CHECK(s.host.stats().unknown_peer >= 1);
    CHECK(s.host.slot_count() == 0);
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 2 * kSec));
}

// Review finding: endless "new nonce from same endpoint" joins must exhaust ids safely, never wrap to 0.
TEST(id_exhaustion_rejects_instead_of_wrapping) {
    test::Session s;
    UdpSocket spammer;
    CHECK(spammer.open(Endpoint::loopback(0)));
    const auto host_ep = Endpoint::loopback(s.host.port());
    for (std::uint64_t nonce = 1; nonce <= 66'000; ++nonce) {
        spammer.send_to(host_ep, encode(Header{}, ConnectRequest{nonce, 0}));
        if (nonce % 64 == 0) s.host.update(s.clk.now);  // drain before the socket buffer overflows
    }
    s.run(kSec / 10);
    CHECK(s.host.running());
    CHECK(s.host.stats().joins <= 65'535);
    for (const auto& e : s.host.world().entities()) CHECK(e.id != 0);
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Rejected; }, 2 * kSec));
    CHECK(c.reject_reason() == RejectReason::ServerFull);
}

TEST(clean_disconnect_frees_the_slot) {
    test::Session s;
    auto& a = s.add_client();
    auto& b = s.add_client();
    CHECK(s.pump_until([&] { return s.all_connected(); }, 2 * kSec));
    const auto old_player = a.player_id();
    a.disconnect();
    CHECK(s.pump_until([&] { return s.host.slot_count() == 1; }, kSec));
    CHECK(s.host.world().entities().size() == 1);
    CHECK(s.pump_until([&] { return b.remote_entities().empty(); }, kSec));
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 2 * kSec));
    CHECK(c.player_id() != old_player);  // player ids are not reused
}

// Test 12
TEST(clean_shutdown_notifies_clients_and_releases_socket) {
    std::uint16_t port = 0;
    std::vector<std::unique_ptr<client::Client>> keep;
    {
        test::Session s;
        auto& a = s.add_client();
        auto& b = s.add_client();
        CHECK(s.pump_until([&] { return s.all_connected(); }, 2 * kSec));
        port = s.host.port();
        s.host.shutdown();
        CHECK(!s.host.running());
        test::run_for(s.clk, nullptr, s.ptrs(), kSec / 4);
        CHECK(a.state() == ClientState::Disconnected && a.disconnect_reason() == DisconnectReason::HostShutdown);
        CHECK(b.state() == ClientState::Disconnected);
        s.host.update(s.clk.now);  // no-op after shutdown, must not crash
        for (auto& c : s.clients) c->close();
    }
    UdpSocket again;
    std::string err;
    CHECK(again.open(Endpoint::loopback(port), &err));  // port released
}

// ---------------------------------------------------------------------------------------------
// Review #1 regression tests (ChatGPT FB-1-01, FB-1-02, FB-1-07). A raw-socket "fake client"
// gives exact control over what the host receives and when.
namespace {
constexpr std::uint64_t kTickUs = 1'000'000 / 30;  // default host tick rate

struct FakeClient {
    UdpSocket sock;
    Endpoint host;
    Header h;  // session filled in after accept
    ConnectAccept accept{};

    explicit FakeClient(std::uint16_t host_port) : host(Endpoint::loopback(host_port)) { CHECK(sock.open(Endpoint::loopback(0))); }
    void send(const Message& m) {
        ++h.sequence;
        CHECK(sock.send_to(host, encode(h, m)));
    }
    // Sends ConnectRequest and steps the host until the matching ConnectAccept arrives.
    void join(test::Session& s, std::uint64_t nonce, std::uint64_t token = 0) {
        send(ConnectRequest{nonce, token});
        for (int i = 0; i < 20; ++i) {
            s.host.update(s.clk.now);
            Endpoint from;
            while (auto p = recv_packet(sock, from)) {
                if (const auto* a = std::get_if<ConnectAccept>(&p->message); a && a->client_nonce == nonce) {
                    accept = *a;
                    h.session = p->header.session;
                    return;
                }
            }
            s.clk.now += kTickUs;
        }
        throw test::CheckFailure("fake client was not accepted");
    }
    // Drains the socket and returns the newest snapshot, if any.
    std::optional<Snapshot> latest_snapshot() {
        std::optional<Snapshot> best;
        std::array<std::uint8_t, 2048> buf{};
        Endpoint from;
        for (int idle = 0; idle < 200;) {
            const auto r = sock.recv_from(from, buf);
            if (r.status != RecvStatus::Data) {
                ++idle;
                std::this_thread::yield();
                continue;
            }
            idle = 0;
            auto d = decode({buf.data(), r.size});
            if (d.packet)
                if (const auto* snap = std::get_if<Snapshot>(&d.packet->message);
                    snap && (!best || seq_newer(snap->server_tick, best->server_tick)))
                    best = *snap;
        }
        return best;
    }
};

InputBatch inputs(std::uint32_t first_seq, std::size_t n, float mx) {
    InputBatch b;
    for (std::size_t i = 0; i < n; ++i) b.inputs.push_back({first_seq + static_cast<std::uint32_t>(i), mx, 0.0f});
    return b;
}

float spawn_x(std::uint16_t entity_id) { return static_cast<float>(entity_id - 1) * 2.0f; }
}  // namespace

// FB-1-01 (P0): a burst of queued inputs must never advance an entity by more than one
// simulation timestep per server tick; the excess backlog is shed instead of simulated.
TEST(input_burst_never_exceeds_one_timestep_per_tick) {
    test::Session s;
    FakeClient fc(s.host.port());
    fc.join(s, 42);
    const auto player = fc.accept.player_id;
    fc.send(inputs(1, 8, 1.0f));
    fc.send(inputs(9, 8, 1.0f));  // 16 inputs queued before the next tick
    const float step = sim::kMoveSpeed / 30.0f;
    float x = s.host.player_transform(player)->x;
    for (int tick = 0; tick < 10; ++tick) {
        const auto world_tick = s.host.world().tick();
        s.clk.now += kTickUs;
        s.host.update(s.clk.now);
        CHECK(s.host.world().tick() == world_tick + 1);  // exactly one server tick per step
        const float nx = s.host.player_transform(player)->x;
        CHECK(nx - x <= step + 1e-4f);  // never more than one timestep of movement per tick
        x = nx;
    }
    const auto snap = fc.latest_snapshot();
    CHECK(snap.has_value());
    CHECK(snap->last_input_seq == 16);  // the backlog fully drained and was acknowledged
    // Policy: at most kTargetInputBacklog (2) inputs stay queued after each tick's input, so
    // exactly 3 inputs are simulated and 13 are shed (and reported to the client as consumed).
    CHECK(s.host.stats().shed_inputs == 13);
    CHECK_NEAR(x - spawn_x(fc.accept.entity_id), 3 * step, 1e-4);
}

// FB-1-01: a steady one-input-per-tick stream is never shed.
TEST(steady_inputs_are_never_shed) {
    test::Session s;
    FakeClient fc(s.host.port());
    fc.join(s, 7);
    for (std::uint32_t seq = 1; seq <= 60; ++seq) {
        fc.send(inputs(seq, 1, 1.0f));
        s.clk.now += kTickUs;
        s.host.update(s.clk.now);
    }
    CHECK(s.host.stats().shed_inputs == 0);
    CHECK_NEAR(s.host.player_transform(fc.accept.player_id)->x - spawn_x(fc.accept.entity_id), 60 * sim::kMoveSpeed / 30.0f,
               1e-3);
}

// FB-1-02: wrong-direction messages are invalid and must not keep a peer alive.
TEST(wrong_direction_messages_do_not_refresh_liveness) {
    test::Session s;
    FakeClient fc(s.host.port());
    fc.join(s, 9);
    Snapshot bogus;
    bogus.server_tick = 1;
    const auto invalid_before = s.host.stats().invalid_packets;
    for (int i = 0; i < 150; ++i) {  // ~5 s of host-to-client-only message types sent by the client
        fc.send(i % 2 ? Message{bogus} : Message{HeartbeatAck{1}});
        s.clk.now += kTickUs;
        s.host.update(s.clk.now);
    }
    CHECK(s.host.stats().timeouts == 1);
    CHECK(s.host.connected_count() == 0);
    CHECK(s.host.stats().invalid_packets >= invalid_before + 80);
}

TEST(client_ignores_wrong_direction_messages_for_liveness) {
    UdpSocket fake;
    CHECK(fake.open(Endpoint::loopback(0)));
    client::ClientConfig cc;
    cc.host = fake.local_endpoint();
    client::Client c(cc, test::test_logger("client"));
    CHECK(c.start());
    test::SimClock clk;
    c.connect(clk.now);
    c.update(clk.now);
    Endpoint client_ep;
    auto req = recv_packet(fake, client_ep);
    CHECK(req.has_value());
    Header h;
    h.session = 5;
    CHECK(fake.send_to(client_ep, encode(h, ConnectAccept{std::get<ConnectRequest>(req->message).client_nonce, 1, 1, 5, 30, 0})));
    test::pump(clk, nullptr, {&c}, [&] { return c.state() == ClientState::Connected; }, kSec);
    CHECK(c.state() == ClientState::Connected);
    for (int i = 0; i < 100 && c.state() == ClientState::Connected; ++i) {  // only host-bound message types
        ++h.sequence;
        fake.send_to(client_ep, encode(h, i % 2 ? Message{Heartbeat{1}} : Message{inputs(1, 1, 0.0f)}));
        clk.now += 50'000;
        c.update(clk.now);
    }
    CHECK(c.state() == ClientState::Reconnecting);
    CHECK(c.stats().invalid_packets >= 50);
}

// FB-1-07: a valid reconnect token moves the slot to a new endpoint (documented: the token is not a credential).
TEST(reconnect_token_migrates_slot_to_new_endpoint) {
    test::Session s;
    FakeClient a(s.host.port());
    a.join(s, 1);
    FakeClient b(s.host.port());
    b.join(s, 2, a.accept.reconnect_token);
    CHECK(b.accept.player_id == a.accept.player_id);
    CHECK(b.accept.entity_id == a.accept.entity_id);
    CHECK(s.host.slot_count() == 1);
    const auto unknown_before = s.host.stats().unknown_peer;
    a.send(Heartbeat{1});  // the old endpoint is no longer a member of the session
    s.host.update(s.clk.now);
    CHECK(s.host.stats().unknown_peer == unknown_before + 1);
}

// FB-1-07: a token whose grace period expired is unknown, so the client re-joins as a new player.
TEST(reconnect_after_grace_expiry_gets_new_identity) {
    host::HostConfig hc;
    hc.reconnect_grace_us = 1 * kSec;
    test::Session s(hc);
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 2 * kSec));
    const auto old_player = c.player_id();
    ImpairmentConfig blackout;
    blackout.loss = 1.0;
    c.set_impairment(blackout);
    CHECK(s.pump_until([&] { return s.host.slot_count() == 0; }, 8 * kSec));  // timed out, then grace expired
    c.set_impairment({});
    // Only the client's uplink was cut, so it still believed it was connected; it notices once the
    // host stops sending to the expired slot (client timeout), then rejoins with an unknown token.
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected && c.stats().reconnects == 1; }, 8 * kSec));
    CHECK(c.player_id() != old_player);
    CHECK(c.stats().reconnects == 1);
}

// FB-1-07: host restart on the same port: the client's old token is unknown to the new session.
TEST(client_survives_host_restart_with_new_session) {
    test::SimClock clk;
    Logger& hl = test::test_logger("host");
    auto h1 = std::make_unique<host::Host>(host::HostConfig{}, hl);
    CHECK(h1->start());
    const auto port = h1->port();
    const auto old_session = h1->session_id();
    client::ClientConfig cc;
    cc.host = Endpoint::loopback(port);
    client::Client c(cc, test::test_logger("client"));
    CHECK(c.start());
    c.connect(clk.now);
    CHECK(test::pump(clk, h1.get(), {&c}, [&] { return c.state() == ClientState::Connected; }, 2 * kSec));
    ImpairmentConfig blackout;
    blackout.loss = 1.0;
    h1->set_impairment(blackout);
    h1->shutdown();  // crash-like: the shutdown notice is lost
    h1.reset();
    CHECK(test::pump(clk, nullptr, {&c}, [&] { return c.state() == ClientState::Reconnecting; }, 5 * kSec));
    host::HostConfig hc2;
    hc2.bind = Endpoint::loopback(port);
    host::Host h2(hc2, hl);
    CHECK(h2.start());
    CHECK(test::pump(clk, &h2, {&c}, [&] { return c.state() == ClientState::Connected && c.predicted_self(); }, 3 * kSec));
    CHECK(c.session_id() == h2.session_id());
    CHECK(c.session_id() != old_session);
    CHECK(h2.stats().joins == 1);
}

// KI-12 (found by the real-time soak, EXP-010): after a one-way outage the client may still believe it is
// connected (buffered snapshots refreshed its liveness) while the host has timed it out. The host now tells
// a timed-out endpoint so, and the client reconnects immediately with its token instead of waiting 3 s.
TEST(timed_out_client_is_told_and_reconnects_fast) {
    test::Session s;
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 2 * kSec));
    const auto player = c.player_id();
    ImpairmentConfig uplink_cut;
    uplink_cut.loss = 1.0;
    c.set_impairment(uplink_cut);
    CHECK(s.pump_until([&] { return s.host.connected_count() == 0; }, 5 * kSec));  // host timed it out
    CHECK(c.state() == ClientState::Connected);  // the client has not noticed yet
    c.set_impairment({});
    const auto restored = s.clk.now;
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected && c.stats().reconnects == 1; }, 3 * kSec));
    CHECK(s.clk.now - restored < kSec / 2);  // previously ~3 s (client-side timeout)
    CHECK(c.player_id() == player);         // same identity via the token
    CHECK(s.host.stats().timeout_notices >= 1);
}

// The timeout notice is rate-limited and only goes to endpoints of slots in their grace period.
TEST(timeout_notice_is_rate_limited_and_scoped) {
    test::Session s;
    FakeClient fc(s.host.port());
    fc.join(s, 11);
    s.clk.now += 4 * kSec;  // silence: the slot times out at the next update
    s.host.update(s.clk.now);
    CHECK(s.host.connected_count() == 0);
    for (int i = 0; i < 10; ++i) fc.send(Heartbeat{1});  // 10 packets within one update
    s.host.update(s.clk.now);
    CHECK(s.host.stats().timeout_notices == 1);
    FakeClient stranger(s.host.port());
    stranger.h.session = fc.h.session;
    stranger.send(Heartbeat{1});  // right session, unknown endpoint: no reply
    s.clk.now += kSec;
    s.host.update(s.clk.now);
    CHECK(s.host.stats().timeout_notices == 1);
}
