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
    UDP_ONLY("uses a raw UDP socket as a fake peer; a GNS host/client only talks GNS");
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
    UDP_ONLY("sends raw UDP garbage; GNS validates its own wire format below our layer");
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
    UDP_ONLY("uses a raw UDP socket as a fake peer; a GNS host/client only talks GNS");
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
    UDP_ONLY("checks UDP port release by re-binding a raw socket");
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
    UDP_ONLY("uses a raw UDP socket as a fake peer; a GNS host/client only talks GNS");
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
    UDP_ONLY("uses a raw UDP socket as a fake peer; a GNS host/client only talks GNS");
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
    UDP_ONLY("uses a raw UDP socket as a fake peer; a GNS host/client only talks GNS");
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
    UDP_ONLY("uses a raw UDP socket as a fake peer; a GNS host/client only talks GNS");
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
    UDP_ONLY("uses a raw UDP socket as a fake peer; a GNS host/client only talks GNS");
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
    auto h1 = std::make_unique<host::Host>(test::Session::with_transport({}), hl);
    CHECK(h1->start());
    const auto port = h1->port();
    const auto old_session = h1->session_id();
    client::ClientConfig cc;
    cc.host = Endpoint::loopback(port);
    cc.transport = test::test_transport();
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
    host::Host h2(test::Session::with_transport(hc2), hl);
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
    UDP_ONLY("asymmetric (uplink-only) outage exercising the KI-12 notice; GNS fake loss is process-global, so both "
             "directions are cut and the transport itself reports the loss (see symmetric_outage_recovers_fast_with_same_identity)");
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

// Both directions cut until the host times the player out, then restored: on every backend the client must
// recover quickly with the same identity (UDP via the timeout notice / our timers, GNS via transport events).
TEST(symmetric_outage_recovers_fast_with_same_identity) {
    test::Session s;
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 3 * kSec));
    const auto player = c.player_id();
    ImpairmentConfig cut;
    cut.loss = 1.0;
    c.set_impairment(cut);
    s.host.set_impairment(cut);
    CHECK(s.pump_until([&] { return s.host.connected_count() == 0; }, 6 * kSec));
    c.set_impairment({});
    s.host.set_impairment({});
    const auto restored = s.clk.now;
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected && c.stats().reconnects == 1; }, 6 * kSec));
    std::printf("       %s: recovered %llu ms after the network came back\n", to_string(test::test_transport()),
                static_cast<unsigned long long>((s.clk.now - restored) / 1000));
    CHECK(s.clk.now - restored < 4 * kSec);  // bounded by one client timeout + one retry interval
    CHECK(c.player_id() == player);         // same identity via the token
    CHECK(s.host.stats().reconnects == 1);
}

// The timeout notice is rate-limited and only goes to endpoints of slots in their grace period.
TEST(timeout_notice_is_rate_limited_and_scoped) {
    UDP_ONLY("uses a raw UDP socket as a fake peer; a GNS host/client only talks GNS");
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

// ---------------------------------------------------------------------------------------------
// Review #2 FB-2-02: the unauthenticated timeout notice must only act when it is current. A fake host
// gives exact control over sequence numbers, sessions and timing.
namespace {
struct FakeHost {
    UdpSocket sock;
    Endpoint client_ep;
    Header h;
    std::uint64_t nonce = 0;
    FakeHost() { CHECK(sock.open(Endpoint::loopback(0))); h.session = 500; }
    // Waits for the client's next ConnectRequest and remembers its nonce.
    void expect_request(client::Client& c, test::SimClock& clk) {
        for (int i = 0; i < 200; ++i) {
            c.update(clk.now);
            Endpoint from;
            std::array<std::uint8_t, 2048> buf{};
            for (auto r = sock.recv_from(from, buf); r.status == RecvStatus::Data; r = sock.recv_from(from, buf)) {
                auto d = decode({buf.data(), r.size});
                if (d.packet)
                    if (const auto* req = std::get_if<ConnectRequest>(&d.packet->message)) {
                        client_ep = from;
                        nonce = req->client_nonce;
                        return;
                    }
            }
            clk.now += 10'000;
            std::this_thread::yield();
        }
        throw test::CheckFailure("no ConnectRequest from client");
    }
    std::vector<std::uint8_t> packet(const Message& m) {
        ++h.sequence;
        return encode(h, m);
    }
    void deliver(client::Client& c, test::SimClock& clk, const std::vector<std::uint8_t>& bytes) {
        CHECK(sock.send_to(client_ep, bytes));
        for (int i = 0; i < 50; ++i) {
            c.update(clk.now);
            std::this_thread::yield();
        }
    }
    void accept(client::Client& c, test::SimClock& clk) { deliver(c, clk, packet(ConnectAccept{nonce, 1, 1, 77, 30, 0})); }
};

struct NoticeFixture {
    test::SimClock clk;
    FakeHost fh;
    client::Client c;
    NoticeFixture() : c(make_cfg(fh), test::test_logger("client")) {
        CHECK(c.start());
        c.connect(clk.now);
        fh.expect_request(c, clk);
        fh.accept(c, clk);
        CHECK(c.state() == ClientState::Connected);
    }
    static client::ClientConfig make_cfg(const FakeHost& fh) {
        client::ClientConfig cc;
        cc.host = fh.sock.local_endpoint();
        return cc;
    }
};
}  // namespace

// The key case from review #2: a notice sent while the slot was in grace, but delivered after the client
// already reconnected, must not knock the fresh connection back into Reconnecting.
TEST(delayed_timeout_notice_after_reconnect_is_ignored) {
    UDP_ONLY("uses a raw UDP socket as a fake peer; a GNS host/client only talks GNS");
    NoticeFixture f;
    const auto notice = f.fh.packet(Disconnect{DisconnectReason::Timeout});
    f.fh.deliver(f.c, f.clk, notice);
    CHECK(f.c.state() == ClientState::Reconnecting);  // a current notice is honoured
    f.fh.expect_request(f.c, f.clk);
    f.fh.accept(f.c, f.clk);
    CHECK(f.c.state() == ClientState::Connected);
    CHECK(f.c.stats().reconnects == 1);
    f.fh.deliver(f.c, f.clk, notice);  // the same (now stale) datagram arrives again late
    CHECK(f.c.state() == ClientState::Connected);
    CHECK(f.c.stats().stale_notices == 1);
    f.fh.deliver(f.c, f.clk, f.fh.packet(Disconnect{DisconnectReason::Timeout}));  // a genuinely new one still works
    CHECK(f.c.state() == ClientState::Reconnecting);
}

TEST(replayed_and_duplicate_timeout_notices_have_no_extra_effect) {
    UDP_ONLY("uses a raw UDP socket as a fake peer; a GNS host/client only talks GNS");
    NoticeFixture f;
    const auto notice = f.fh.packet(Disconnect{DisconnectReason::Timeout});
    for (int i = 0; i < 5; ++i) f.fh.deliver(f.c, f.clk, notice);  // duplicates while already reconnecting
    CHECK(f.c.state() == ClientState::Reconnecting);
    f.fh.expect_request(f.c, f.clk);
    f.fh.accept(f.c, f.clk);
    for (int i = 0; i < 5; ++i) f.fh.deliver(f.c, f.clk, notice);  // replays after reconnecting
    CHECK(f.c.state() == ClientState::Connected);
    CHECK(f.c.stats().reconnects == 1);
}

TEST(timeout_notice_from_other_session_or_endpoint_is_ignored) {
    UDP_ONLY("uses a raw UDP socket as a fake peer; a GNS host/client only talks GNS");
    NoticeFixture f;
    Header old = f.fh.h;
    old.session = 499;  // a previous host session
    old.sequence = f.fh.h.sequence + 100;
    f.fh.deliver(f.c, f.clk, encode(old, Disconnect{DisconnectReason::Timeout}));
    CHECK(f.c.state() == ClientState::Connected);
    UdpSocket other;
    CHECK(other.open(Endpoint::loopback(0)));
    Header spoof = f.fh.h;
    spoof.sequence += 100;
    CHECK(other.send_to(f.fh.client_ep, encode(spoof, Disconnect{DisconnectReason::Timeout})));
    for (int i = 0; i < 50; ++i) {
        f.c.update(f.clk.now);
        std::this_thread::yield();
    }
    CHECK(f.c.state() == ClientState::Connected);  // wrong source endpoint
    CHECK(f.c.stats().foreign_packets >= 2);
}

// ---------------------------------------------------------------------------------------------
// Review #5 FB-5-04: the raw-UDP fake-peer tests above check OUR protocol rules (validation, wrong direction,
// sequence guards, input shedding, token migration). These substitutes check the same rules through a real
// connection of the backend under test, so they run on every backend. What cannot be expressed on a
// connection-oriented backend is listed in docs/test-coverage.md (for example a stranger injecting into an
// existing connection: GNS itself rejects that below our layer).
namespace {
void settle() {  // lets real-time delivery happen (GNS runs its own thread); a no-op cost on UDP
    std::this_thread::sleep_for(std::chrono::milliseconds(test::test_transport() == TransportKind::Udp ? 1 : 20));
}

std::unique_ptr<Transport> open_transport(bool listen) {
    std::string err;
    auto t = make_transport(test::test_transport(), {}, &err);
    if (!t || !t->open(Endpoint::loopback(0), listen, &err)) throw test::CheckFailure("transport open failed: " + err);
    return t;
}

std::size_t count_if_kind(const std::vector<TransportEvent>& ev, TransportEvent::Kind k) {
    return static_cast<std::size_t>(std::count_if(ev.begin(), ev.end(), [&](const TransportEvent& e) { return e.kind == k; }));
}

std::vector<std::pair<PeerId, Packet>> drain(Transport& t, std::vector<TransportEvent>* events = nullptr) {
    std::vector<TransportEvent> ev;
    t.poll(0, ev);
    std::vector<std::pair<PeerId, Packet>> out;
    for (auto& e : ev) {
        if (e.kind == TransportEvent::Kind::Message)
            if (auto d = decode(e.bytes); d.packet) out.emplace_back(e.peer, *d.packet);
        if (events) events->push_back(std::move(e));
    }
    return out;
}

struct TransportFakeClient {
    std::unique_ptr<Transport> t = open_transport(false);
    PeerId peer;
    Header h;
    ConnectAccept accept{};
    explicit TransportFakeClient(std::uint16_t host_port) { peer = t->connect(Endpoint::loopback(host_port)).value(); }
    void send(const Message& m) {
        ++h.sequence;
        CHECK(t->send(peer, encode(h, m), Delivery::Unreliable, 0) == SendResult::Sent);
    }
    // Retries ConnectRequest (like a real client) and steps the host until the matching ConnectAccept arrives.
    void join(test::Session& s, std::uint64_t nonce, std::uint64_t token = 0) {
        for (int i = 0; i < 200; ++i) {
            if (i % 5 == 0) send(ConnectRequest{nonce, token});
            settle();
            s.host.update(s.clk.now);
            settle();
            for (auto& [from, p] : drain(*t))
                if (const auto* a = std::get_if<ConnectAccept>(&p.message); a && a->client_nonce == nonce) {
                    accept = *a;
                    h.session = p.header.session;
                    return;
                }
        }
        throw test::CheckFailure("fake client was not accepted");
    }
    std::optional<Snapshot> latest_snapshot() {
        std::optional<Snapshot> best;
        settle();
        for (auto& [from, p] : drain(*t))
            if (const auto* snap = std::get_if<Snapshot>(&p.message); snap && (!best || seq_newer(snap->server_tick, best->server_tick)))
                best = *snap;
        return best;
    }
};

struct TransportFakeHost {
    std::unique_ptr<Transport> t = open_transport(true);
    PeerId client;  // current connection of the client (a GNS reconnect is a new one)
    Header h;
    std::uint64_t nonce = 0;
    TransportFakeHost() { h.session = 500; }
    Endpoint endpoint() const { return Endpoint::loopback(t->local_endpoint().port); }
    void expect_request(client::Client& c, test::SimClock& clk) {
        for (int i = 0; i < 400; ++i) {
            c.update(clk.now);
            settle();
            for (auto& [from, p] : drain(*t))
                if (const auto* req = std::get_if<ConnectRequest>(&p.message)) {
                    client = from;
                    nonce = req->client_nonce;
                    return;
                }
            clk.now += 10'000;
        }
        throw test::CheckFailure("no ConnectRequest from client");
    }
    std::vector<std::uint8_t> packet(const Message& m) {
        ++h.sequence;
        return encode(h, m);
    }
    // Returns false when the connection it targets no longer exists. Only a connection-oriented backend can say
    // that: the peer closed it (for example the client reconnecting on a new connection), so nothing stale can
    // arrive on it any more.
    bool deliver(client::Client& c, const test::SimClock& clk, const std::vector<std::uint8_t>& bytes) {
        const auto r = t->send(client, bytes, Delivery::Unreliable, 0);
        CHECK(r == SendResult::Sent || (r == SendResult::NotConnected && t->capabilities().reports_disconnects));
        if (r != SendResult::Sent) return false;
        for (int i = 0; i < 10; ++i) {
            settle();
            c.update(clk.now);
            drain(*t);  // keep our side serviced
        }
        return true;
    }
    void accept(client::Client& c, const test::SimClock& clk) { deliver(c, clk, packet(ConnectAccept{nonce, 1, 1, 77, 30, 0})); }
};

struct TransportNoticeFixture {
    test::SimClock clk;
    TransportFakeHost fh;
    client::Client c;
    TransportNoticeFixture() : c(make_cfg(fh), test::test_logger("client")) {
        CHECK(c.start());
        c.connect(clk.now);
        fh.expect_request(c, clk);
        fh.accept(c, clk);
        CHECK(c.state() == ClientState::Connected);
    }
    static client::ClientConfig make_cfg(const TransportFakeHost& fh) {
        client::ClientConfig cc;
        cc.host = fh.endpoint();
        cc.transport = test::test_transport();
        return cc;
    }
};
}  // namespace

TEST(host_survives_invalid_payloads_over_transport) {
    test::Session s;
    TransportFakeClient attacker(s.host.port());
    std::mt19937 rng(99);
    std::vector<std::vector<std::uint8_t>> junk = {{1, 2, 3}, std::vector<std::uint8_t>(1200, 0xAB)};
    auto valid = encode(Header{}, ConnectRequest{1, 0});
    valid.push_back(0);  // length mismatch
    junk.push_back(valid);
    for (int i = 0; i < 200; ++i) {
        std::vector<std::uint8_t> r(1 + rng() % 100);
        for (auto& b : r) b = static_cast<std::uint8_t>(rng());
        junk.push_back(r);
    }
    InputBatch b;  // well-formed, but from a peer that never joined
    b.inputs.push_back({1, 1, 0});
    junk.push_back(encode(Header{}, b));
    // Reliable where offered, so every payload is known to arrive; plain datagrams on loopback otherwise.
    const auto d = attacker.t->capabilities().reliable_ordered ? Delivery::ReliableOrdered : Delivery::Unreliable;
    for (const auto& j : junk) CHECK(attacker.t->send(attacker.peer, j, d, 0) == SendResult::Sent);
    for (int i = 0; i < 50; ++i) {
        settle();
        s.host.update(s.clk.now);
        s.clk.now += kTickUs;
        drain(*attacker.t);
    }
    CHECK(s.host.running());
    CHECK(s.host.stats().invalid_packets >= 200);
    CHECK(s.host.stats().unknown_peer >= 1);
    CHECK(s.host.slot_count() == 0);
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 3 * kSec));
}

TEST(client_rejects_stale_snapshots_over_transport) {
    TransportNoticeFixture f;
    Snapshot snap;
    snap.entities.push_back({1, 1, {10, 0, 0, 0}, 0, 0});
    snap.entities.push_back({2, 2, {20, 0, 0, 0}, 0, 0});
    snap.server_tick = 10;
    f.fh.deliver(f.c, f.clk, f.fh.packet(snap));
    CHECK(f.c.stats().snapshots_applied == 1);
    snap.server_tick = 9;
    snap.entities[0].transform.x = -50;
    f.fh.deliver(f.c, f.clk, f.fh.packet(snap));  // older
    snap.server_tick = 10;
    f.fh.deliver(f.c, f.clk, f.fh.packet(snap));  // duplicate
    CHECK(f.c.stats().snapshots_applied == 1);
    CHECK(f.c.stats().stale_snapshots == 2);
    CHECK(f.c.authoritative_self()->x == 10.0f);
    Header other = f.fh.h;
    other.session = 78;  // wrong session is ignored entirely
    other.sequence += 1;
    snap.server_tick = 11;
    f.fh.deliver(f.c, f.clk, encode(other, snap));
    CHECK(f.c.stats().snapshots_applied == 1);
}

TEST(client_ignores_wrong_direction_messages_over_transport) {
    TransportNoticeFixture f;
    for (int i = 0; i < 100 && f.c.state() == ClientState::Connected; ++i) {  // only host-bound message types
        CHECK(f.fh.t->send(f.fh.client, f.fh.packet(i % 2 ? Message{Heartbeat{1}} : Message{inputs(1, 1, 0.0f)}),
                           Delivery::Unreliable, 0) == SendResult::Sent);
        settle();
        f.clk.now += 50'000;
        f.c.update(f.clk.now);
        drain(*f.fh.t);
    }
    CHECK(f.c.state() == ClientState::Reconnecting);
    CHECK(f.c.stats().invalid_packets >= 50);
}

TEST(host_ignores_wrong_direction_messages_over_transport) {
    test::Session s;
    TransportFakeClient fc(s.host.port());
    fc.join(s, 9);
    Snapshot bogus;
    bogus.server_tick = 1;
    const auto invalid_before = s.host.stats().invalid_packets;
    for (int i = 0; i < 150; ++i) {  // ~5 s of host-to-client-only message types sent by the client
        fc.send(i % 2 ? Message{bogus} : Message{HeartbeatAck{1}});
        settle();
        s.clk.now += kTickUs;
        s.host.update(s.clk.now);
        drain(*fc.t);
    }
    CHECK(s.host.stats().timeouts == 1);
    CHECK(s.host.connected_count() == 0);
    CHECK(s.host.stats().invalid_packets >= invalid_before + 80);
}

TEST(input_burst_is_shed_over_transport) {
    test::Session s;
    TransportFakeClient fc(s.host.port());
    fc.join(s, 42);
    const auto player = fc.accept.player_id;
    fc.send(inputs(1, 8, 1.0f));
    fc.send(inputs(9, 8, 1.0f));  // 16 inputs queued before the next tick
    settle();
    s.host.update(s.clk.now);  // receive both without a tick
    const float step = sim::kMoveSpeed / 30.0f;
    float x = s.host.player_transform(player)->x;
    for (int tick = 0; tick < 10; ++tick) {
        const auto world_tick = s.host.world().tick();
        s.clk.now += kTickUs;
        s.host.update(s.clk.now);
        CHECK(s.host.world().tick() == world_tick + 1);
        const float nx = s.host.player_transform(player)->x;
        CHECK(nx - x <= step + 1e-4f);
        x = nx;
    }
    const auto snap = fc.latest_snapshot();
    CHECK(snap.has_value());
    CHECK(snap->last_input_seq == 16);
    CHECK(s.host.stats().shed_inputs == 13);
    CHECK_NEAR(x - spawn_x(fc.accept.entity_id), 3 * step, 1e-4);
}

TEST(reconnect_token_moves_slot_to_new_connection_over_transport) {
    test::Session s;
    TransportFakeClient a(s.host.port());
    a.join(s, 1);
    TransportFakeClient b(s.host.port());
    b.join(s, 2, a.accept.reconnect_token);
    CHECK(b.accept.player_id == a.accept.player_id);
    CHECK(b.accept.entity_id == a.accept.entity_id);
    CHECK(s.host.slot_count() == 1);
    const auto unknown_before = s.host.stats().unknown_peer;
    ++a.h.sequence;  // the old connection is no longer a member of the session
    const auto r = a.t->send(a.peer, encode(a.h, Heartbeat{1}), Delivery::Unreliable, 0);
    if (a.t->capabilities().reports_disconnects) {
        // Connection-oriented: the host closed the old connection when the slot moved, so the stale member
        // cannot even send any more. Stronger than the UDP rule below.
        std::vector<TransportEvent> ev;
        for (int i = 0; i < 100 && count_if_kind(ev, TransportEvent::Kind::Disconnected) == 0; ++i) {
            settle();
            s.host.update(s.clk.now);
            drain(*a.t, &ev);
        }
        CHECK(count_if_kind(ev, TransportEvent::Kind::Disconnected) == 1 || r == SendResult::NotConnected);
        CHECK(a.t->send(a.peer, encode(a.h, Heartbeat{1}), Delivery::Unreliable, 0) == SendResult::NotConnected);
    } else {
        CHECK(r == SendResult::Sent);
        settle();
        s.host.update(s.clk.now);
        CHECK(s.host.stats().unknown_peer == unknown_before + 1);
    }
}

TEST(timeout_notice_is_rate_limited_and_scoped_over_transport) {
    test::Session s;
    TransportFakeClient fc(s.host.port());
    fc.join(s, 11);
    s.clk.now += 4 * kSec;  // simulated silence: the slot times out at the next update (the connection stays up)
    s.host.update(s.clk.now);
    CHECK(s.host.connected_count() == 0);
    for (int i = 0; i < 10; ++i) fc.send(Heartbeat{1});
    settle();
    s.host.update(s.clk.now);
    CHECK(s.host.stats().timeout_notices == 1);
    TransportFakeClient stranger(s.host.port());  // right session, but a connection that never joined
    stranger.h.session = fc.h.session;
    stranger.send(Heartbeat{1});
    settle();
    s.clk.now += kSec;
    s.host.update(s.clk.now);
    CHECK(s.host.stats().timeout_notices == 1);
}

// Delayed, replayed and duplicate notices, and one from another session. On a connection-oriented backend the
// replay arrives on the client's NEW connection, so only the sequence guard can reject it.
TEST(stale_replayed_and_foreign_session_notices_are_ignored_over_transport) {
    TransportNoticeFixture f;
    Header old = f.fh.h;
    old.session = 499;  // a previous host session
    old.sequence = f.fh.h.sequence + 100;
    f.fh.deliver(f.c, f.clk, encode(old, Disconnect{DisconnectReason::Timeout}));
    CHECK(f.c.state() == ClientState::Connected);
    const auto notice = f.fh.packet(Disconnect{DisconnectReason::Timeout});
    CHECK(f.fh.deliver(f.c, f.clk, notice));  // a current notice is honoured
    CHECK(f.c.state() == ClientState::Reconnecting);
    for (int i = 0; i < 2; ++i) f.fh.deliver(f.c, f.clk, notice);  // duplicates (GNS: the old connection is gone)
    CHECK(f.c.state() == ClientState::Reconnecting);
    f.fh.expect_request(f.c, f.clk);
    f.fh.accept(f.c, f.clk);
    CHECK(f.c.state() == ClientState::Connected);
    CHECK(f.c.stats().reconnects == 1);
    for (int i = 0; i < 3; ++i) CHECK(f.fh.deliver(f.c, f.clk, notice));  // the old notice replayed late, on the
    CHECK(f.c.state() == ClientState::Connected);                           // current connection
    CHECK(f.c.stats().stale_notices == 3);  // only the sequence guard stands between the replay and a reconnect
    CHECK(f.fh.deliver(f.c, f.clk, f.fh.packet(Disconnect{DisconnectReason::Timeout})));  // a new one still works
    CHECK(f.c.state() == ClientState::Reconnecting);
}

// ---------------------------------------------------------------------------------------------
// Review #9 FB-9-02 / KI-28: application-level reliable events (protocol v2), on every backend.
namespace {
std::vector<std::uint8_t> ev_payload(std::uint32_t n) { return {static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(n >> 8)}; }
std::uint32_t ev_value(const client::ReceivedEvent& e) { return e.payload[0] | (std::uint32_t{e.payload[1]} << 8); }
}  // namespace

TEST(events_are_applied_once_in_order_and_forgotten_after_the_application_ack) {
    test::Session s;
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 3 * kSec));
    const auto player = c.player_id();
    for (std::uint32_t i = 1; i <= 30; ++i) CHECK(s.host.send_event(player, 5, ev_payload(i), s.clk.now));
    std::vector<std::uint32_t> applied;
    CHECK(s.pump_until([&] {
        for (const auto& e : c.take_events()) {
            applied.push_back(ev_value(e));
            c.ack_events_applied(e.id, s.clk.now);  // the application applied it
        }
        return applied.size() == 30 && s.host.unapplied_events(player) == 0;
    }, 5 * kSec));
    for (std::uint32_t i = 0; i < applied.size(); ++i) CHECK(applied[i] == i + 1);
    CHECK(s.host.stats().events_applied == 30);
}

TEST(host_keeps_sending_until_the_application_ack_not_the_transport_ack) {
    test::Session s;
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 3 * kSec));
    CHECK(s.host.send_event(c.player_id(), 1, ev_payload(1), s.clk.now));
    std::vector<client::ReceivedEvent> got;
    CHECK(s.pump_until([&] {
        for (auto& e : c.take_events()) got.push_back(e);
        return !got.empty();
    }, 3 * kSec));
    s.run(kSec);  // delivered (and transport-acked where reliable), but NOT applied by the application yet
    CHECK(s.host.unapplied_events(c.player_id()) == 1);
    c.ack_events_applied(got[0].id, s.clk.now);
    CHECK(s.pump_until([&] { return s.host.unapplied_events(c.player_id()) == 0; }, 2 * kSec));
}

// Delivered-but-unapplied events, and events queued while the player was away, all arrive exactly once and in
// order after a reconnect (the host replays everything unapplied on the new connection).
TEST(unapplied_and_queued_events_survive_an_outage_and_reconnect) {
    test::Session s;
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 3 * kSec));
    const auto player = c.player_id();
    for (std::uint32_t i = 1; i <= 5; ++i) CHECK(s.host.send_event(player, 1, ev_payload(i), s.clk.now));
    std::vector<std::uint32_t> held;  // delivered to the application, which applies 1..3 now and 4..5 later
    CHECK(s.pump_until([&] {
        for (const auto& e : c.take_events()) held.push_back(ev_value(e));
        return held.size() == 5;
    }, 3 * kSec));
    c.ack_events_applied(3, s.clk.now);
    CHECK(s.pump_until([&] { return s.host.unapplied_events(player) == 2; }, 2 * kSec));
    ImpairmentConfig cut;
    cut.loss = 1.0;
    CHECK(c.set_impairment(cut));
    CHECK(s.host.set_impairment(cut));
    CHECK(s.pump_until([&] { return s.host.connected_count() == 0; }, 8 * kSec));
    for (std::uint32_t i = 6; i <= 8; ++i) CHECK(s.host.send_event(player, 1, ev_payload(i), s.clk.now));  // while away
    CHECK(c.set_impairment({}));
    CHECK(s.host.set_impairment({}));
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected && c.stats().reconnects >= 1; }, 15 * kSec));
    CHECK(c.player_id() == player);
    std::vector<std::uint32_t> later;
    CHECK(s.pump_until([&] {
        for (const auto& e : c.take_events()) later.push_back(ev_value(e));
        return later.size() >= 3;
    }, 5 * kSec));
    s.run(kSec / 2);
    for (const auto& e : c.take_events()) later.push_back(ev_value(e));
    CHECK((later == std::vector<std::uint32_t>{6, 7, 8}));  // 4 and 5 were replayed too, but dropped as duplicates
    CHECK(c.stats().events_duplicate >= 2);
    CHECK(s.host.stats().events_replayed >= 2);
    c.ack_events_applied(8, s.clk.now);  // the application applies 4..8
    CHECK(s.pump_until([&] { return s.host.unapplied_events(player) == 0; }, 3 * kSec));
    std::printf("       %s: events replayed %llu, duplicates dropped %llu, applied 1..8 exactly once\n",
                to_string(test::test_transport()), static_cast<unsigned long long>(s.host.stats().events_replayed),
                static_cast<unsigned long long>(c.stats().events_duplicate));
}

TEST(events_beyond_the_window_are_refused_explicitly) {
    host::HostConfig hc;
    hc.max_unacked_events = 8;
    test::Session s(hc);
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 3 * kSec));
    for (std::uint32_t i = 1; i <= 8; ++i) CHECK(s.host.send_event(c.player_id(), 1, ev_payload(i), s.clk.now));
    CHECK(!s.host.send_event(c.player_id(), 1, ev_payload(9), s.clk.now));  // backpressure, not a silent drop
    CHECK(s.host.stats().events_backpressure == 1);
    CHECK(!s.host.send_event(999, 1, ev_payload(1), s.clk.now));  // unknown player
    CHECK(!s.host.send_event(c.player_id(), 1, std::vector<std::uint8_t>(kMaxEventPayload + 1), s.clk.now));
}

// A restarted host is a new session: its event ids start again at 1 and must not be mistaken for duplicates.
// Events of the old session are not durable (documented); the client starts a fresh stream.
TEST(a_host_restart_starts_a_new_event_stream) {
    test::SimClock clk;
    Logger& hl = test::test_logger("host");
    auto h1 = std::make_unique<host::Host>(test::Session::with_transport({}), hl);
    CHECK(h1->start());
    const auto port = h1->port();
    client::ClientConfig cc;
    cc.host = Endpoint::loopback(port);
    cc.transport = test::test_transport();
    client::Client c(cc, test::test_logger("client"));
    CHECK(c.start());
    c.connect(clk.now);
    CHECK(test::pump(clk, h1.get(), {&c}, [&] { return c.state() == ClientState::Connected; }, 2 * kSec));
    for (std::uint32_t i = 1; i <= 3; ++i) CHECK(h1->send_event(c.player_id(), 1, ev_payload(100 + i), clk.now));
    std::size_t old_events = 0;
    CHECK(test::pump(clk, h1.get(), {&c}, [&] {
        old_events += c.take_events().size();
        return old_events == 3;
    }, 2 * kSec));
    c.ack_events_applied(3, clk.now);
    ImpairmentConfig blackout;
    blackout.loss = 1.0;
    h1->set_impairment(blackout);
    h1->shutdown();
    h1.reset();
    CHECK(test::pump(clk, nullptr, {&c}, [&] { return c.state() == ClientState::Reconnecting; }, 5 * kSec));
    host::HostConfig hc2;
    hc2.bind = Endpoint::loopback(port);
    host::Host h2(test::Session::with_transport(hc2), hl);
    CHECK(h2.start());
    CHECK(test::pump(clk, &h2, {&c}, [&] { return c.state() == ClientState::Connected; }, 3 * kSec));
    CHECK(h2.send_event(c.player_id(), 2, ev_payload(1), clk.now));
    std::vector<client::ReceivedEvent> fresh;
    CHECK(test::pump(clk, &h2, {&c}, [&] {
        for (auto& e : c.take_events()) fresh.push_back(e);
        return !fresh.empty();
    }, 2 * kSec));
    CHECK(fresh.size() == 1 && fresh[0].id == 1 && ev_value(fresh[0]) == 1);  // id 1 again: accepted, not a duplicate
    CHECK(c.events_applied_through() == 0);  // the new session starts with nothing applied
}

// EXP-022 regression: the host resends an unapplied event on EVERY transport, including reliable ones. (The soak
// found a client that discarded an event which overtook its ConnectAccept; on a reliable transport the host had
// never resent it, so the stream stalled behind that gap for the whole run.)
TEST(unapplied_events_are_resent_even_on_reliable_transports) {
    test::Session s;
    auto& c = s.add_client();
    CHECK(s.pump_until([&] { return c.state() == ClientState::Connected; }, 3 * kSec));
    CHECK(s.host.send_event(c.player_id(), 1, ev_payload(1), s.clk.now));
    s.run(3 * kSec);  // delivered, never applied: the application never acks
    const auto delivered = c.take_events();
    CHECK(delivered.size() == 1);  // exactly once to the application, despite the resends
    std::printf("       %s: unapplied event resent %llu times in 3 s; client dropped %llu duplicates\n",
                to_string(test::test_transport()), static_cast<unsigned long long>(s.host.stats().events_retransmitted),
                static_cast<unsigned long long>(c.stats().events_duplicate));
    CHECK(s.host.stats().events_retransmitted >= 1);
    CHECK(c.stats().events_duplicate >= 1);
}

// Review #10 FB-10-01: an EventAck can only cover events the host actually sent; a future/forged ack is refused whole
// and never erases undelivered events. A stale ack is harmless.
TEST(event_acks_beyond_what_was_sent_are_refused) {
    test::Session s;
    TransportFakeClient fc(s.host.port());
    fc.join(s, 77);
    const auto player = fc.accept.player_id;
    for (std::uint32_t i = 1; i <= 3; ++i) CHECK(s.host.send_event(player, 1, ev_payload(i), s.clk.now));
    settle();
    s.host.update(s.clk.now);
    fc.send(EventAck{100});  // claims far more than was ever sent
    settle();
    s.host.update(s.clk.now);
    CHECK(s.host.unapplied_events(player) == 3);
    CHECK(s.host.stats().event_acks_invalid == 1);
    fc.send(EventAck{2});  // valid
    settle();
    s.host.update(s.clk.now);
    CHECK(s.host.unapplied_events(player) == 1);
    fc.send(EventAck{1});  // stale: nothing to do, not invalid
    settle();
    s.host.update(s.clk.now);
    CHECK(s.host.unapplied_events(player) == 1 && s.host.stats().event_acks_invalid == 1);
    // Queued after the last transmission (the host has not sent it yet when the ack arrives): never erased.
    CHECK(s.host.send_event(player, 1, ev_payload(4), s.clk.now));  // sent at once, highest_sent = 4
    fc.send(EventAck{5});
    settle();
    s.host.update(s.clk.now);
    CHECK(s.host.unapplied_events(player) == 2 && s.host.stats().event_acks_invalid == 2);
}

// Review #10 FB-10-04 / KI-37: explicit leave handshake (protocol v3).
TEST(leave_is_confirmed_by_the_host) {
    test::Session s;
    auto& a = s.add_client();
    auto& b = s.add_client();
    CHECK(s.pump_until([&] { return s.all_connected(); }, 2 * kSec));
    a.leave(s.clk.now);
    CHECK(a.leaving());
    CHECK(s.pump_until([&] { return !a.leaving(); }, 2 * kSec));
    CHECK(a.stats().leave_confirmed);
    CHECK(a.state() == ClientState::Disconnected);
    CHECK(s.host.slot_count() == 1 && s.host.stats().clean_leaves == 1);
    CHECK(b.state() == ClientState::Connected);
}

TEST(host_repeats_disconnect_ack_for_a_released_slot) {
    test::Session s;
    TransportFakeClient fc(s.host.port());
    fc.join(s, 91);
    auto acks = [&] {
        int n = 0;
        for (int i = 0; i < 10; ++i) {
            settle();
            s.host.update(s.clk.now);
            settle();
            for (auto& [from, p] : drain(*fc.t))
                if (std::holds_alternative<DisconnectAck>(p.message)) ++n;
        }
        return n;
    };
    fc.send(Disconnect{DisconnectReason::ClientQuit});
    CHECK(acks() >= 1);
    CHECK(s.host.slot_count() == 0 && s.host.stats().clean_leaves == 1);
    // The first DisconnectAck was lost: the client repeats its Disconnect and is confirmed again. The host keeps
    // the transport peer open during the leave window on every backend (on GNS closing it at once let the close
    // notice overtake the acks, KI-37). Repeats are answered at most once per 100 ms.
    fc.send(Disconnect{DisconnectReason::ClientQuit});
    CHECK(acks() == 0);  // same instant as the first ack: rate-limited
    s.clk.now += 150'000;
    fc.send(Disconnect{DisconnectReason::ClientQuit});
    CHECK(acks() >= 1);
    CHECK(s.host.stats().leave_acks_repeated == 1 && s.host.stats().clean_leaves == 1);
    // A stranger (not in a leave window) sending a Disconnect with the session id gets nothing: no reflection.
    TransportFakeClient stranger(s.host.port());
    stranger.h.session = fc.h.session;
    stranger.send(Disconnect{DisconnectReason::ClientQuit});
    for (int i = 0; i < 10; ++i) {
        settle();
        s.host.update(s.clk.now);
        settle();
        for (auto& [from, p] : drain(*stranger.t)) CHECK(!std::holds_alternative<DisconnectAck>(p.message));
    }
    CHECK(s.host.stats().leave_acks_repeated == 1);
    s.clk.now += 150'000;
    fc.h.session ^= 1;  // a Disconnect for another session is not acknowledged
    fc.send(Disconnect{DisconnectReason::ClientQuit});
    CHECK(acks() == 0 && s.host.stats().leave_acks_repeated == 1);
    if (fc.t->capabilities().reports_disconnects) {  // ...and closes it after the window
        bool closed = false;
        for (int i = 0; i < 400 && !closed; ++i) {
            s.clk.now += 10'000;
            s.host.update(s.clk.now);
            settle();
            std::vector<TransportEvent> ev;
            fc.t->poll(0, ev);
            for (const auto& e : ev) closed |= e.kind == TransportEvent::Kind::Disconnected;
        }
        CHECK(closed);
    }
}

TEST(unconfirmed_leave_ends_after_the_timeout) {
    test::SimClock clk;
    TransportFakeHost fh;
    client::ClientConfig cc;
    cc.host = fh.endpoint();
    cc.transport = test::test_transport();
    client::Client c(cc, test::test_logger("client"));
    CHECK(c.start());
    c.connect(clk.now);
    fh.expect_request(c, clk);
    fh.accept(c, clk);
    CHECK(c.state() == ClientState::Connected);
    c.leave(clk.now);
    while (c.leaving() && clk.now < 10 * kSec) {
        clk.now += 10'000;
        c.update(clk.now);
        settle();
        drain(*fh.t);  // the fake host never answers
    }
    CHECK(c.state() == ClientState::Disconnected);
    CHECK(!c.stats().leave_confirmed);
    CHECK(c.stats().leave_ms >= 1000 && c.stats().leave_ms < 1100);
    CHECK(c.stats().leave_disconnects_sent >= 10);
}
