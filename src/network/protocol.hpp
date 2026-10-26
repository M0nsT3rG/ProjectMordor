#pragma once
// Project Mordor prototype wire protocol (see docs/protocol.md).
// Little-endian, fixed 22-byte header. The header layout and the ConnectReject
// payload are FROZEN across protocol versions so mismatched peers can still be rejected cleanly.
// This protocol is deliberately independent of any game-internal representation.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace mordor::net {

inline constexpr std::array<std::uint8_t, 4> kMagic{'M', 'R', 'D', 'R'};
// v2 (2026-10-24): GameEvent / EventAck, application-level reliable events (review #9 FB-9-02, KI-28).
// v3 (2026-10-24): DisconnectAck, explicit leave handshake (review #10 FB-10-04, KI-37).
inline constexpr std::uint16_t kProtocolVersion = 4;  // v4: events, acks and leave messages carry their player
inline constexpr std::size_t kHeaderSize = 22;
inline constexpr std::size_t kMaxPacketSize = 1200;  // stays under common internet MTU
inline constexpr std::size_t kMaxPayloadSize = kMaxPacketSize - kHeaderSize;
inline constexpr std::uint8_t kMaxPlayers = 2;
inline constexpr std::size_t kMaxInputsPerPacket = 8;
inline constexpr std::size_t kMaxEntitiesPerSnapshot = 16;
inline constexpr float kMaxCoordinate = 1.0e6f;
inline constexpr std::size_t kMaxEventPayload = 256;

enum class MsgType : std::uint8_t {
    ConnectRequest = 1,
    ConnectAccept = 2,
    ConnectReject = 3,
    Disconnect = 4,
    Heartbeat = 5,
    HeartbeatAck = 6,
    Input = 7,
    Snapshot = 8,
    Event = 9,     // host -> client: a consequential game event (v2)
    EventAck = 10, // client -> host: every event up to this id has been APPLIED by the client application (v2)
    DisconnectAck = 11, // host -> client: your Disconnect was received and your slot released (v3)
};

enum class RejectReason : std::uint8_t { VersionMismatch = 1, ServerFull = 2, Malformed = 3 };
enum class DisconnectReason : std::uint8_t { ClientQuit = 1, HostShutdown = 2, Timeout = 3, Kicked = 4 };

struct Header {
    std::uint16_t version = kProtocolVersion;
    MsgType type{};            // set by encode()
    std::uint8_t flags = 0;
    std::uint32_t session = 0;  // 0 until the host assigns one
    std::uint32_t sequence = 0; // per-direction packet counter
    std::uint32_t ack = 0;      // latest sequence received from the peer (informational)
    std::uint16_t payload_len = 0;  // set by encode()
};

struct Transform {
    float x = 0, y = 0, z = 0, yaw = 0;
    bool operator==(const Transform&) const = default;
};

struct ConnectRequest {
    std::uint64_t client_nonce = 0;
    std::uint64_t reconnect_token = 0;  // 0 = new player
    bool operator==(const ConnectRequest&) const = default;
};
struct ConnectAccept {
    std::uint64_t client_nonce = 0;  // echoes the request, so stale accepts from earlier attempts are ignored
    std::uint16_t player_id = 0;
    std::uint16_t entity_id = 0;
    std::uint64_t reconnect_token = 0;
    std::uint16_t tick_rate_hz = 0;
    std::uint32_t server_tick = 0;
    bool operator==(const ConnectAccept&) const = default;
};
struct ConnectReject {
    RejectReason reason{};
    std::uint16_t host_version = kProtocolVersion;
    bool operator==(const ConnectReject&) const = default;
};
struct Disconnect {
    DisconnectReason reason{};
    std::uint16_t player = 0;  // v4: the player leaving (client) or being told (host); see GameEvent
    bool operator==(const Disconnect&) const = default;
};
struct Heartbeat {
    std::uint64_t send_time_us = 0;
    bool operator==(const Heartbeat&) const = default;
};
struct HeartbeatAck {
    std::uint64_t echo_time_us = 0;
    bool operator==(const HeartbeatAck&) const = default;
};
struct InputCmd {
    std::uint32_t input_seq = 0;
    float move_x = 0, move_z = 0;  // each in [-1, 1]
    bool operator==(const InputCmd&) const = default;
};
struct InputBatch {
    std::vector<InputCmd> inputs;  // oldest first; redundant copies cover packet loss
    bool operator==(const InputBatch&) const = default;
};
struct EntityState {
    std::uint16_t entity_id = 0;
    std::uint16_t owner_player = 0;
    Transform transform;
    float vel_x = 0, vel_z = 0;
    bool operator==(const EntityState&) const = default;
};
struct Snapshot {
    std::uint32_t server_tick = 0;
    std::uint32_t last_input_seq = 0;  // last input of the receiving player applied by the host
    std::vector<EntityState> entities;
    bool operator==(const Snapshot&) const = default;
};

// Application-level reliable event (review #9 FB-9-02). Ids start at 1 per player and session and are
// contiguous; the client delivers them in order exactly once and acknowledges only what its application applied.
// Both carry the player whose stream they belong to (v4, Codex PR #24): on UDP a player who leaves and rejoins the
// same session reuses its peer, and a delayed event or ack of the old stream must never count for the new one.
struct GameEvent {
    std::uint32_t event_id = 0;
    std::uint16_t kind = 0;
    std::vector<std::uint8_t> payload;  // at most kMaxEventPayload bytes
    std::uint16_t player = 0;
    bool operator==(const GameEvent&) const = default;
};
struct EventAck {
    std::uint32_t applied_through = 0;
    std::uint16_t player = 0;
    bool operator==(const EventAck&) const = default;
};
// Confirms a client's Disconnect (review #10 FB-10-04): a clean leave is only "clean" once the host said so.
struct DisconnectAck {
    std::uint16_t player = 0;  // v4: whose leave this confirms
    bool operator==(const DisconnectAck&) const = default;
};

using Message = std::variant<ConnectRequest, ConnectAccept, ConnectReject, Disconnect, Heartbeat, HeartbeatAck,
                             InputBatch, Snapshot, GameEvent, EventAck, DisconnectAck>;

struct Packet {
    Header header;
    Message message;
};

enum class DecodeError { None, TooShort, TooLong, BadMagic, VersionMismatch, UnknownType, LengthMismatch, BadPayload };
const char* to_string(DecodeError e);

struct DecodeResult {
    DecodeError error = DecodeError::None;
    std::optional<Packet> packet;
    std::uint16_t peer_version = 0;  // valid once the magic matched
    std::uint8_t peer_type = 0;
};

MsgType type_of(const Message& m);

// Returns an empty vector if the message violates protocol limits.
std::vector<std::uint8_t> encode(Header h, const Message& m);
DecodeResult decode(std::span<const std::uint8_t> bytes);

// True if sequence a is more recent than b, tolerating 32-bit wraparound.
constexpr bool seq_newer(std::uint32_t a, std::uint32_t b) {
    return a != b && static_cast<std::uint32_t>(a - b) < 0x80000000u;
}

}  // namespace mordor::net
