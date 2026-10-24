#include "network/protocol.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace mordor::net {
namespace {

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

struct Writer {
    std::vector<std::uint8_t>& b;
    void u8(std::uint8_t v) { b.push_back(v); }
    void u16(std::uint16_t v) { u8(static_cast<std::uint8_t>(v)); u8(static_cast<std::uint8_t>(v >> 8)); }
    void u32(std::uint32_t v) { u16(static_cast<std::uint16_t>(v)); u16(static_cast<std::uint16_t>(v >> 16)); }
    void u64(std::uint64_t v) { u32(static_cast<std::uint32_t>(v)); u32(static_cast<std::uint32_t>(v >> 32)); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void transform(const Transform& t) { f32(t.x); f32(t.y); f32(t.z); f32(t.yaw); }
};

struct Reader {
    std::span<const std::uint8_t> d;
    std::size_t p = 0;
    bool ok = true;
    std::uint8_t u8() {
        if (p >= d.size()) { ok = false; return 0; }
        return d[p++];
    }
    std::uint16_t u16() { std::uint16_t lo = u8(); return static_cast<std::uint16_t>(lo | (u8() << 8)); }
    std::uint32_t u32() { std::uint32_t lo = u16(); return lo | (static_cast<std::uint32_t>(u16()) << 16); }
    std::uint64_t u64() { std::uint64_t lo = u32(); return lo | (static_cast<std::uint64_t>(u32()) << 32); }
    float f32() { return std::bit_cast<float>(u32()); }
    Transform transform() { Transform t; t.x = f32(); t.y = f32(); t.z = f32(); t.yaw = f32(); return t; }
    bool done() const { return ok && p == d.size(); }
};

bool coord_ok(float v) { return std::isfinite(v) && std::fabs(v) <= kMaxCoordinate; }
bool axis_ok(float v) { return std::isfinite(v) && v >= -1.0f && v <= 1.0f; }
bool transform_ok(const Transform& t) { return coord_ok(t.x) && coord_ok(t.y) && coord_ok(t.z) && coord_ok(t.yaw); }

bool write_payload(Writer& w, const Message& m) {
    return std::visit(overloaded{
        [&](const ConnectRequest& v) { w.u64(v.client_nonce); w.u64(v.reconnect_token); return true; },
        [&](const ConnectAccept& v) {
            w.u64(v.client_nonce); w.u16(v.player_id); w.u16(v.entity_id); w.u64(v.reconnect_token); w.u16(v.tick_rate_hz); w.u32(v.server_tick);
            return true;
        },
        [&](const ConnectReject& v) { w.u8(static_cast<std::uint8_t>(v.reason)); w.u16(v.host_version); return true; },
        [&](const Disconnect& v) { w.u8(static_cast<std::uint8_t>(v.reason)); return true; },
        [&](const Heartbeat& v) { w.u64(v.send_time_us); return true; },
        [&](const HeartbeatAck& v) { w.u64(v.echo_time_us); return true; },
        [&](const InputBatch& v) {
            if (v.inputs.empty() || v.inputs.size() > kMaxInputsPerPacket) return false;
            w.u8(static_cast<std::uint8_t>(v.inputs.size()));
            for (const auto& c : v.inputs) { w.u32(c.input_seq); w.f32(c.move_x); w.f32(c.move_z); }
            return true;
        },
        [&](const Snapshot& v) {
            if (v.entities.size() > kMaxEntitiesPerSnapshot) return false;
            w.u32(v.server_tick); w.u32(v.last_input_seq); w.u8(static_cast<std::uint8_t>(v.entities.size()));
            for (const auto& e : v.entities) {
                w.u16(e.entity_id); w.u16(e.owner_player); w.transform(e.transform); w.f32(e.vel_x); w.f32(e.vel_z);
            }
            return true;
        },
        [&](const GameEvent& v) {
            if (v.event_id == 0 || v.payload.size() > kMaxEventPayload) return false;
            w.u32(v.event_id); w.u16(v.kind); w.u16(static_cast<std::uint16_t>(v.payload.size()));
            for (auto b : v.payload) w.u8(b);
            return true;
        },
        [&](const EventAck& v) { w.u32(v.applied_through); return true; },
        [&](const DisconnectAck&) { return true; },
    }, m);
}

std::optional<Message> read_payload(MsgType t, Reader& r) {
    switch (t) {
        case MsgType::ConnectRequest: {
            ConnectRequest v; v.client_nonce = r.u64(); v.reconnect_token = r.u64();
            return v;
        }
        case MsgType::ConnectAccept: {
            ConnectAccept v;
            v.client_nonce = r.u64(); v.player_id = r.u16(); v.entity_id = r.u16(); v.reconnect_token = r.u64(); v.tick_rate_hz = r.u16(); v.server_tick = r.u32();
            if (v.player_id == 0 || v.entity_id == 0 || v.tick_rate_hz == 0 || v.tick_rate_hz > 240) return std::nullopt;
            return v;
        }
        case MsgType::ConnectReject: {
            const auto reason = r.u8();
            ConnectReject v; v.host_version = r.u16();
            if (reason < 1 || reason > 3) return std::nullopt;
            v.reason = static_cast<RejectReason>(reason);
            return v;
        }
        case MsgType::Disconnect: {
            const auto reason = r.u8();
            if (reason < 1 || reason > 4) return std::nullopt;
            return Disconnect{static_cast<DisconnectReason>(reason)};
        }
        case MsgType::Heartbeat: return Heartbeat{r.u64()};
        case MsgType::HeartbeatAck: return HeartbeatAck{r.u64()};
        case MsgType::Input: {
            const auto n = r.u8();
            if (n == 0 || n > kMaxInputsPerPacket) return std::nullopt;
            InputBatch v;
            for (int i = 0; i < n && r.ok; ++i) {
                InputCmd c; c.input_seq = r.u32(); c.move_x = r.f32(); c.move_z = r.f32();
                if (!axis_ok(c.move_x) || !axis_ok(c.move_z)) return std::nullopt;
                v.inputs.push_back(c);
            }
            return v;
        }
        case MsgType::Snapshot: {
            Snapshot v; v.server_tick = r.u32(); v.last_input_seq = r.u32();
            const auto n = r.u8();
            if (n > kMaxEntitiesPerSnapshot) return std::nullopt;
            for (int i = 0; i < n && r.ok; ++i) {
                EntityState e; e.entity_id = r.u16(); e.owner_player = r.u16(); e.transform = r.transform();
                e.vel_x = r.f32(); e.vel_z = r.f32();
                if (e.entity_id == 0 || !transform_ok(e.transform) || !coord_ok(e.vel_x) || !coord_ok(e.vel_z)) return std::nullopt;
                v.entities.push_back(e);
            }
            return v;
        }
        case MsgType::Event: {
            GameEvent v; v.event_id = r.u32(); v.kind = r.u16();
            const auto n = r.u16();
            if (v.event_id == 0 || n > kMaxEventPayload) return std::nullopt;
            for (int i = 0; i < n && r.ok; ++i) v.payload.push_back(r.u8());
            return v;
        }
        case MsgType::EventAck: return EventAck{r.u32()};
        case MsgType::DisconnectAck: return DisconnectAck{};
    }
    return std::nullopt;
}

}  // namespace

const char* to_string(DecodeError e) {
    switch (e) {
        case DecodeError::None: return "none";
        case DecodeError::TooShort: return "too_short";
        case DecodeError::TooLong: return "too_long";
        case DecodeError::BadMagic: return "bad_magic";
        case DecodeError::VersionMismatch: return "version_mismatch";
        case DecodeError::UnknownType: return "unknown_type";
        case DecodeError::LengthMismatch: return "length_mismatch";
        case DecodeError::BadPayload: return "bad_payload";
    }
    return "?";
}

MsgType type_of(const Message& m) { return static_cast<MsgType>(m.index() + 1); }

std::vector<std::uint8_t> encode(Header h, const Message& m) {
    std::vector<std::uint8_t> payload;
    Writer pw{payload};
    if (!write_payload(pw, m) || payload.size() > kMaxPayloadSize) return {};
    h.type = type_of(m);
    h.payload_len = static_cast<std::uint16_t>(payload.size());
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + payload.size());
    Writer w{out};
    for (auto c : kMagic) w.u8(c);
    w.u16(h.version); w.u8(static_cast<std::uint8_t>(h.type)); w.u8(h.flags);
    w.u32(h.session); w.u32(h.sequence); w.u32(h.ack); w.u16(h.payload_len);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

DecodeResult decode(std::span<const std::uint8_t> bytes) {
    DecodeResult res;
    if (bytes.size() < kHeaderSize) { res.error = DecodeError::TooShort; return res; }
    if (bytes.size() > kMaxPacketSize) { res.error = DecodeError::TooLong; return res; }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) { res.error = DecodeError::BadMagic; return res; }
    Reader r{bytes.first(kHeaderSize), kMagic.size()};
    Header h;
    h.version = r.u16();
    const auto type = r.u8();
    h.flags = r.u8(); h.session = r.u32(); h.sequence = r.u32(); h.ack = r.u32(); h.payload_len = r.u16();
    res.peer_version = h.version;
    res.peer_type = type;
    const bool frozen = type == static_cast<std::uint8_t>(MsgType::ConnectReject);
    if (h.version != kProtocolVersion && !frozen) { res.error = DecodeError::VersionMismatch; return res; }
    if (type < 1 || type > 11) { res.error = DecodeError::UnknownType; return res; }
    h.type = static_cast<MsgType>(type);
    if (h.payload_len != bytes.size() - kHeaderSize) { res.error = DecodeError::LengthMismatch; return res; }
    Reader pr{bytes.subspan(kHeaderSize)};
    auto msg = read_payload(h.type, pr);
    if (!msg || !pr.done()) { res.error = DecodeError::BadPayload; return res; }
    res.packet = Packet{h, std::move(*msg)};
    return res;
}

}  // namespace mordor::net
