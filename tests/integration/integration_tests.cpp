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
