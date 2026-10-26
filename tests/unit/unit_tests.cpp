// Unit tests: protocol encoding/validation, sequence logic, simulation and interpolation. No sockets.
#include <limits>
#include <numbers>
#include <random>

#include "client/event_order.hpp"
#include "common/cli.hpp"
#include "test_support.hpp"

using namespace mordor;
using namespace mordor::net;

namespace {

Header hdr(std::uint32_t seq = 7) {
    Header h;
    h.session = 0xABCD1234;
    h.sequence = seq;
    h.ack = 3;
    return h;
}

template <class T>
T roundtrip(const T& msg) {
    const auto bytes = encode(hdr(), msg);
    CHECK(!bytes.empty());
    const auto d = decode(bytes);
    CHECK(d.error == DecodeError::None);
    CHECK(d.packet->header.session == 0xABCD1234u && d.packet->header.sequence == 7 && d.packet->header.ack == 3);
    CHECK(d.packet->header.type == type_of(msg));
    const T* out = std::get_if<T>(&d.packet->message);
    CHECK(out != nullptr);
    return *out;
}

Snapshot sample_snapshot() {
    Snapshot s;
    s.server_tick = 0xFFFFFFF0u;
    s.last_input_seq = 42;
    s.entities.push_back({1, 1, {1.5f, -0.0f, -99.25f, 3.14159f}, 5.0f, -5.0f});
    s.entities.push_back({2, 2, {kMaxCoordinate, 0.0f, -kMaxCoordinate, -3.0f}, 0.0f, 0.0f});
    return s;
}

}  // namespace

// Test 4 (unit part): transforms survive the wire bit-exactly, including -0 and limits.
TEST(json_escape_handles_quotes_backslashes_and_control_characters) {
    // FB-22-01: the runner's results file must stay valid JSON whatever --suite or an error message contains.
    using test::json_escape;
    CHECK(json_escape("plain_name") == "plain_name");
    CHECK(json_escape("a\"b") == "a\\\"b");
    CHECK(json_escape("C:\\dir\\file") == "C:\\\\dir\\\\file");
    CHECK(json_escape("line1\nline2\r\tend") == "line1\\nline2\\r\\tend");
    CHECK(json_escape(std::string_view("\x01\x1f", 2)) == "\\u0001\\u001f");
    CHECK(json_escape(std::string_view("nul\0x", 5)) == "nul\\u0000x");
    CHECK(json_escape("M\xC3\xBAsica") == "M\xC3\xBAsica");  // UTF-8 passes through unchanged
}

TEST(transform_snapshot_roundtrip) {
    const auto s = sample_snapshot();
    const auto out = roundtrip(s);
    CHECK(out == s);
    CHECK(std::signbit(out.entities[0].transform.y));
}

TEST(all_message_types_roundtrip) {
    CHECK(roundtrip(ConnectRequest{0x1122334455667788ull, 99}) == (ConnectRequest{0x1122334455667788ull, 99}));
    CHECK(roundtrip(ConnectAccept{77, 1, 2, 3, 30, 400}) == (ConnectAccept{77, 1, 2, 3, 30, 400}));
    CHECK(roundtrip(ConnectReject{RejectReason::ServerFull, 1}) == (ConnectReject{RejectReason::ServerFull, 1}));
    CHECK(roundtrip(Disconnect{DisconnectReason::Timeout}) == (Disconnect{DisconnectReason::Timeout}));
    CHECK(roundtrip(Heartbeat{123456789}) == (Heartbeat{123456789}));
    CHECK(roundtrip(HeartbeatAck{987654321}) == (HeartbeatAck{987654321}));
    InputBatch b;
    for (std::uint32_t i = 1; i <= kMaxInputsPerPacket; ++i) b.inputs.push_back({i, -1.0f, 1.0f});
    CHECK(roundtrip(b) == b);
}

// Review #9 FB-9-02 (protocol v2): event messages round-trip; limits are enforced on both sides.
TEST(event_messages_roundtrip_and_respect_limits) {
    GameEvent e{7, 42, {1, 2, 3}, 5};
    CHECK(roundtrip(e) == e);
    CHECK(roundtrip(EventAck{123, 5}) == (EventAck{123, 5}));
    CHECK(roundtrip(DisconnectAck{}) == (DisconnectAck{}));
    CHECK(encode(hdr(1), GameEvent{0, 1, {}}).empty());                                       // id 0 is invalid
    CHECK(encode(hdr(1), GameEvent{1, 1, std::vector<std::uint8_t>(kMaxEventPayload + 1)}).empty());  // too large
    auto bytes = encode(hdr(1), GameEvent{1, 1, {9}});
    bytes[kHeaderSize + 8] = 200;  // claimed payload length beyond the packet (id 4, player 2, kind 2 bytes before it)
    CHECK(!decode(bytes).packet);
}

TEST(header_layout_is_22_bytes_little_endian) {
    const auto bytes = encode(hdr(0x01020304), Heartbeat{0});
    CHECK(bytes.size() == kHeaderSize + 8);
    CHECK(bytes[0] == 'M' && bytes[3] == 'R');
    CHECK(bytes[4] == kProtocolVersion && bytes[5] == 0);
    CHECK(bytes[6] == static_cast<std::uint8_t>(MsgType::Heartbeat));
    CHECK(bytes[12] == 0x04 && bytes[15] == 0x01);  // sequence, little-endian
    CHECK(bytes[20] == 8 && bytes[21] == 0);         // payload length
}

// Test 5: sequence comparison with wraparound.
TEST(sequence_newer_handles_wraparound) {
    CHECK(seq_newer(2, 1));
    CHECK(!seq_newer(1, 2));
    CHECK(!seq_newer(5, 5));
    CHECK(seq_newer(0, 0xFFFFFFFFu));
    CHECK(seq_newer(10, 0xFFFFFFF0u));
    CHECK(!seq_newer(0xFFFFFFF0u, 10));
}

// Test 10 (unit part)
TEST(version_mismatch_is_reported) {
    Header h = hdr();
    h.version = kProtocolVersion + 1;
    const auto d = decode(encode(h, ConnectRequest{1, 0}));
    CHECK(d.error == DecodeError::VersionMismatch);
    CHECK(d.peer_version == kProtocolVersion + 1);
    CHECK(d.peer_type == static_cast<std::uint8_t>(MsgType::ConnectRequest));
    CHECK(!d.packet);
}

TEST(connect_reject_is_decodable_across_versions) {
    Header h = hdr();
    h.version = 99;
    const auto d = decode(encode(h, ConnectReject{RejectReason::VersionMismatch, 99}));
    CHECK(d.error == DecodeError::None);
    CHECK(std::get<ConnectReject>(d.packet->message).host_version == 99);
}

// Test 11: invalid sizes and payloads are rejected with a specific error.
TEST(decode_rejects_invalid_sizes_and_payloads) {
    const auto good = encode(hdr(), sample_snapshot());
    CHECK(decode(std::span(good).first(kHeaderSize - 1)).error == DecodeError::TooShort);
    CHECK(decode({}).error == DecodeError::TooShort);
    std::vector<std::uint8_t> huge(kMaxPacketSize + 1, 0);
    CHECK(decode(huge).error == DecodeError::TooLong);

    auto bad_magic = good;
    bad_magic[0] = 'X';
    CHECK(decode(bad_magic).error == DecodeError::BadMagic);

    auto truncated = good;
    truncated.pop_back();
    CHECK(decode(truncated).error == DecodeError::LengthMismatch);

    auto trailing = good;
    trailing.push_back(0);
    CHECK(decode(trailing).error == DecodeError::LengthMismatch);

    auto unknown = good;
    unknown[6] = 200;
    CHECK(decode(unknown).error == DecodeError::UnknownType);

    // Declared length matches, but payload is shorter than the message requires.
    auto short_payload = encode(hdr(), Heartbeat{1});
    short_payload.pop_back();
    short_payload[20] = 7;
    CHECK(decode(short_payload).error == DecodeError::BadPayload);
}

TEST(decode_rejects_semantically_invalid_values) {
    InputBatch nan_input;
    nan_input.inputs.push_back({1, std::numeric_limits<float>::quiet_NaN(), 0});
    CHECK(decode(encode(hdr(), nan_input)).error == DecodeError::BadPayload);

    InputBatch out_of_range;
    out_of_range.inputs.push_back({1, 1.5f, 0});
    CHECK(decode(encode(hdr(), out_of_range)).error == DecodeError::BadPayload);

    auto inf_snap = sample_snapshot();
    inf_snap.entities[0].transform.x = std::numeric_limits<float>::infinity();
    CHECK(decode(encode(hdr(), inf_snap)).error == DecodeError::BadPayload);

    auto zero_id = sample_snapshot();
    zero_id.entities[0].entity_id = 0;
    CHECK(decode(encode(hdr(), zero_id)).error == DecodeError::BadPayload);

    CHECK(decode(encode(hdr(), ConnectAccept{5, 0, 1, 1, 30, 0})).error == DecodeError::BadPayload);
    CHECK(decode(encode(hdr(), ConnectAccept{5, 1, 1, 1, 0, 0})).error == DecodeError::BadPayload);

    auto bad_reason = encode(hdr(), Disconnect{DisconnectReason::ClientQuit});
    bad_reason[kHeaderSize] = 77;
    CHECK(decode(bad_reason).error == DecodeError::BadPayload);
}

TEST(encode_refuses_messages_over_limits) {
    CHECK(encode(hdr(), InputBatch{}).empty());
    InputBatch too_many;
    too_many.inputs.resize(kMaxInputsPerPacket + 1);
    CHECK(encode(hdr(), too_many).empty());
    Snapshot big;
    big.entities.resize(kMaxEntitiesPerSnapshot + 1, EntityState{1, 1, {}, 0, 0});
    CHECK(encode(hdr(), big).empty());

    // Entity count byte claims more entities than the limit.
    auto bytes = encode(hdr(), sample_snapshot());
    bytes[kHeaderSize + 8] = static_cast<std::uint8_t>(kMaxEntitiesPerSnapshot + 1);
    CHECK(decode(bytes).error == DecodeError::BadPayload);
}

// Property test: random and mutated buffers never crash the decoder, and anything accepted re-encodes identically.
TEST(decode_fuzz_never_crashes_and_accepted_packets_reencode) {
    std::mt19937_64 rng(1234);
    std::vector<std::vector<std::uint8_t>> seeds = {encode(hdr(), sample_snapshot()), encode(hdr(), ConnectRequest{5, 6}),
                                                    encode(hdr(), ConnectAccept{5, 1, 1, 9, 30, 1})};
    InputBatch b;
    b.inputs = {{1, 0.5f, -0.5f}, {2, 0, 1}};
    seeds.push_back(encode(hdr(), b));
    int accepted = 0;
    for (int i = 0; i < 200'000; ++i) {
        std::vector<std::uint8_t> buf;
        if (i % 2 == 0) {
            buf.resize(rng() % 64);
            for (auto& x : buf) x = static_cast<std::uint8_t>(rng());
            if (buf.size() >= 4 && i % 4 == 0) std::copy(kMagic.begin(), kMagic.end(), buf.begin());
        } else {
            buf = seeds[rng() % seeds.size()];
            const int flips = 1 + static_cast<int>(rng() % 4);
            for (int f = 0; f < flips; ++f) buf[rng() % buf.size()] ^= static_cast<std::uint8_t>(1u << (rng() % 8));
        }
        const auto d = decode(buf);
        if (d.error == DecodeError::None) {
            ++accepted;
            CHECK(encode(d.packet->header, d.packet->message) == buf);
        }
    }
    CHECK(accepted > 0);  // mutations of ids/sequences are legitimately valid
}

TEST(apply_input_normalizes_and_clamps) {
    sim::Entity e;
    sim::apply_input(e, 1.0f, 1.0f, 1.0f);  // diagonal must not be faster than kMoveSpeed
    CHECK_NEAR(std::hypot(e.t.x, e.t.z), sim::kMoveSpeed, 1e-4);
    CHECK_NEAR(e.t.yaw, std::numbers::pi / 4, 1e-5);
    sim::Entity far;
    for (int i = 0; i < 1000; ++i) sim::apply_input(far, 1.0f, 0.0f, 1.0f);
    CHECK(far.t.x == sim::kWorldHalfExtent);
    sim::Entity big;
    sim::apply_input(big, 50.0f, 0.0f, 1.0f);  // out-of-range input is clamped, not trusted
    CHECK_NEAR(big.t.x, sim::kMoveSpeed, 1e-5);
}

TEST(simulation_replay_is_deterministic) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1, 1);
    std::vector<std::pair<float, float>> inputs(500);
    for (auto& [x, z] : inputs) x = u(rng), z = u(rng);
    sim::Entity a, b;
    for (auto [x, z] : inputs) sim::apply_input(a, x, z, 1.0f / 30);
    for (auto [x, z] : inputs) sim::apply_input(b, x, z, 1.0f / 30);
    CHECK(a.t == b.t);
}

TEST(world_assigns_unique_entity_ids) {
    sim::World w;
    const auto a = w.spawn(1).id, b = w.spawn(2).id;
    CHECK(a != b && a != 0 && b != 0);
    CHECK(w.despawn(a));
    const auto c = w.spawn(3).id;
    CHECK(c != a && c != b);  // ids are never reused within a session
    CHECK(!w.despawn(a));
}

// Test 6: interpolation is consistent: exact at samples, linear between, clamped outside, stale samples ignored.
TEST(interpolation_is_consistent) {
    sim::InterpolationBuffer buf;
    CHECK(!buf.sample(0));
    CHECK(buf.push(10, {0, 0, 0, 0}));
    CHECK(buf.push(12, {2, 4, -2, 0}));
    CHECK(!buf.push(11, {99, 99, 99, 0}));  // older than latest: rejected
    CHECK(!buf.push(12, {99, 99, 99, 0}));  // duplicate: rejected
    CHECK(buf.size() == 2);
    CHECK(*buf.sample(10) == (Transform{0, 0, 0, 0}));
    CHECK(*buf.sample(12) == (Transform{2, 4, -2, 0}));
    const auto mid = *buf.sample(11);
    CHECK_NEAR(mid.x, 1, 1e-6);
    CHECK_NEAR(mid.y, 2, 1e-6);
    CHECK_NEAR(mid.z, -1, 1e-6);
    CHECK(*buf.sample(5) == (Transform{0, 0, 0, 0}));
    CHECK(*buf.sample(50) == (Transform{2, 4, -2, 0}));
    // Monotonic: x never decreases while sampling forward through a monotonic path.
    float last = -1;
    for (double t = 10; t <= 12; t += 0.05) {
        const float x = buf.sample(t)->x;
        CHECK(x >= last);
        last = x;
    }
    for (std::uint32_t t = 13; t < 13 + 2 * sim::InterpolationBuffer::kCapacity; ++t) buf.push(t, {});
    CHECK(buf.size() == sim::InterpolationBuffer::kCapacity);
}

// Review #6 answer 3: the connect deadline is a configurable policy with an explicit range; out of range fails.
TEST(connect_deadline_policy_is_bounded) {
    auto parse = [](std::vector<const char*> argv) {
        argv.insert(argv.begin(), "mordor-client");
        cli::Args a(static_cast<int>(argv.size()), const_cast<char**>(argv.data()));
        return cli::connect_deadline_us_from(a, client::kMinConnectDeadlineUs, client::kMaxConnectDeadlineUs, 20'000'000);
    };
    CHECK(parse({}) == 20'000'000);
    CHECK(parse({"--connect-deadline-s", "30"}) == 30'000'000);
    CHECK(parse({"--connect-deadline-s", "120"}) == 120'000'000);
    for (const char* bad : {"0", "0.5", "121", "-3"}) {
        bool threw = false;
        try {
            parse({"--connect-deadline-s", bad});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(threw);
    }
    client::ClientConfig cc;
    cc.connect_deadline_us = 200'000'000;
    client::Client c(cc, test::test_logger("client"));
    std::string err;
    CHECK(!c.start(&err) && !err.empty());  // the library refuses it too, not only the CLI
}

TEST(interpolation_takes_short_path_for_yaw) {
    const float pi = std::numbers::pi_v<float>;
    const float r = sim::lerp_angle(pi - 0.1f, -pi + 0.1f, 0.5f);
    CHECK_NEAR(std::fabs(r), pi, 1e-4);  // through +-pi, not through 0
}

// Codex (PR #24): the client application's event-order check follows the stream: a reconnect to the same player
// continues it, a new host session or a new player restarts it at id 1, and only real gaps or repeats count.
TEST(event_order_check_follows_the_stream) {
    client::EventOrderCheck c;
    for (std::uint32_t id : {1u, 2u, 3u}) c.apply(7, 1, id);
    c.apply(7, 1, 4);  // a token reconnect to the same player: the same stream
    CHECK(c.errors == 0);
    c.apply(9, 1, 1);  // the host restarted: a new session, ids again from 1
    c.apply(9, 2, 1);  // a new player in that session (grace expired): again from 1
    c.apply(9, 2, 2);
    CHECK(c.errors == 0);
    c.apply(9, 2, 4);  // a gap
    c.apply(9, 2, 4);  // a repeat
    CHECK(c.errors == 2);
}
