#include "host/host.hpp"

#include <algorithm>
#include <array>

namespace mordor::host {

using namespace mordor::net;

namespace {
constexpr std::size_t kMaxPendingInputs = 32;
constexpr std::size_t kTargetInputBacklog = 2;
constexpr int kDisconnectRepeats = 3;  // unreliable channel; repeats are idempotent

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
}  // namespace

Host::Host(HostConfig cfg, Logger& log)
    : cfg_(cfg), log_(log), rng_(std::random_device{}()) {}

std::uint64_t Host::random_nonzero() {
    std::uint64_t v;
    do v = rng_(); while (v == 0);
    return v;
}

bool Host::start(std::string* error) {
    if (cfg_.max_players == 0 || cfg_.max_players > kMaxPlayers || cfg_.tick_rate_hz == 0 || cfg_.tick_rate_hz > 240 ||
        !cfg_.impairment.valid()) {
        if (error) *error = "invalid host configuration";
        return false;
    }
    transport_ = make_transport(cfg_.transport, cfg_.impairment, error);
    if (transport_ && cfg_.secure) transport_ = make_secure_transport(std::move(transport_), *cfg_.secure, error);
    if (!transport_ || !transport_->open(cfg_.bind, true, error)) {
        transport_.reset();
        return false;
    }
    session_ = static_cast<std::uint32_t>(random_nonzero() | 1u);
    running_ = true;
    clock_started_ = false;
    log_.info("host_started", {kv("port", port()), kv("loopback_only", cfg_.bind.is_loopback()), kv("session", session_),
                               kv("secure", cfg_.secure != nullptr),
                               kv("tick_rate_hz", cfg_.tick_rate_hz), kv("protocol_version", kProtocolVersion),
                               kv("transport", to_string(cfg_.transport))});
    return true;
}

std::size_t Host::connected_count() const {
    return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(), [](const Slot& s) { return s.connected; }));
}

std::optional<Transform> Host::player_transform(std::uint16_t player_id) const {
    for (const auto& s : slots_)
        if (s.player_id == player_id)
            for (const auto& e : world_.entities())
                if (e.id == s.entity_id) return e.t;
    return std::nullopt;
}

// Only connected slots match: a slot in its grace period can be reclaimed solely by its reconnect token.
Host::Slot* Host::find_by_peer(PeerId p) {
    auto it = std::find_if(slots_.begin(), slots_.end(), [&](const Slot& s) { return s.connected && s.peer == p; });
    return it == slots_.end() ? nullptr : &*it;
}

void Host::update(std::uint64_t now_us) {
    if (!running_) return;
    const std::uint64_t tick_us = 1'000'000 / cfg_.tick_rate_hz;
    if (!clock_started_) {
        next_tick_us_ = first_update_us_ = now_us;
        clock_started_ = true;
    }
    last_update_us_ = now_us;
    receive(now_us);
    // shortcut: after a stall longer than 5 ticks we skip ahead instead of catching up; revisit for real gameplay.
    if (now_us > next_tick_us_ + 5 * tick_us) {
        stats_.skipped_ticks += (now_us - next_tick_us_) / tick_us;
        next_tick_us_ = now_us;
    }
    while (now_us >= next_tick_us_) {
        step();
        broadcast(now_us);
        next_tick_us_ += tick_us;
    }
    check_timeouts(now_us);
    service_events(now_us);
    transport_->flush(now_us);  // send what is due; receiving happens only at the start of update()
}

bool Host::send_event(std::uint16_t player_id, std::uint16_t kind, std::span<const std::uint8_t> payload, std::uint64_t now_us) {
    if (!running_ || payload.size() > kMaxEventPayload) return false;
    for (auto& s : slots_) {
        if (s.player_id != player_id) continue;
        if (s.events.size() >= cfg_.max_unacked_events) {
            ++stats_.events_backpressure;
            return false;
        }
        s.events.push_back({GameEvent{s.next_event_id++, kind, {payload.begin(), payload.end()}}, 0});
        ++stats_.events_queued;
        service_events(now_us);  // send now if the player is connected
        return true;
    }
    return false;
}

std::vector<std::uint16_t> Host::player_ids() const {
    std::vector<std::uint16_t> ids;
    for (const auto& s : slots_) ids.push_back(s.player_id);
    return ids;
}

std::size_t Host::unapplied_events(std::uint16_t player_id) const {
    for (const auto& s : slots_)
        if (s.player_id == player_id) return s.events.size();
    return 0;
}

// Sends events not yet sent on the current connection and resends every unapplied one periodically, on ANY
// transport: application-level reliability must not depend on transport reliability. A reliable transport may
// deliver an event that the client application then could not accept (e.g. it overtook the ConnectAccept), and
// without a resend the stream stalled behind that gap forever (EXP-022). A reconnect also replays at once.
void Host::service_events(std::uint64_t now_us) {
    const bool reliable = transport_->capabilities().reliable_ordered;
    const auto delivery = reliable ? Delivery::ReliableOrdered : Delivery::Unreliable;
    const auto resend_us = reliable ? std::max<std::uint64_t>(4 * cfg_.event_resend_us, 1'000'000) : cfg_.event_resend_us;
    for (auto& s : slots_) {
        if (!s.connected) continue;  // kept for the grace period, replayed on reconnect
        for (auto& e : s.events) {
            const bool first = e.last_sent_us == 0;
            if (!first && now_us - e.last_sent_us < resend_us) continue;
            if (e.retry_at_us && now_us < e.retry_at_us) continue;  // backing off after a refused send
            // Review #10 FB-10-02: only a send the transport ACCEPTED counts; otherwise retry with bounded back-off.
            if (send(s, e.event, now_us, delivery) != SendResult::Sent) {
                ++stats_.events_send_refused;
                e.retry_at_us = now_us + std::min<std::uint64_t>(10'000ull << std::min(e.refusals++, 6u), 500'000);
                continue;
            }
            e.refusals = 0;
            e.retry_at_us = 0;
            e.last_sent_us = now_us ? now_us : 1;
            s.highest_sent_event = std::max(s.highest_sent_event, e.event.event_id);
            ++(first ? stats_.events_transmitted : stats_.events_retransmitted);
        }
    }
}

void Host::receive(std::uint64_t now_us) {
    events_.clear();
    transport_->poll(now_us, events_);
    for (const auto& e : events_) {
        switch (e.kind) {
            case TransportEvent::Kind::Message: on_datagram(e.peer, e.bytes, now_us); break;
            case TransportEvent::Kind::Disconnected: on_transport_disconnect(e.peer, now_us); break;
            case TransportEvent::Kind::DroppedInbound:
                ++stats_.packets_in;
                ++stats_.invalid_packets;
                log_.debug("invalid_packet", {kv("reason", e.reason)});
                break;
            case TransportEvent::Kind::Connected: break;  // a peer exists only once it sends a valid ConnectRequest
        }
    }
    events_.clear();
}

void Host::on_datagram(PeerId from, std::span<const std::uint8_t> bytes, std::uint64_t now_us) {
    ++stats_.packets_in;
    stats_.bytes_in += bytes.size();
    const auto d = decode(bytes);
    if (d.error == DecodeError::VersionMismatch) {
        ++stats_.rejected_version;
        log_.warn("version_mismatch", {kv("peer_version", d.peer_version), kv("our_version", kProtocolVersion)});
        if (d.peer_type == static_cast<std::uint8_t>(MsgType::ConnectRequest))
            send_unconnected(from, ConnectReject{RejectReason::VersionMismatch, kProtocolVersion}, now_us);
        return;
    }
    if (d.error != DecodeError::None) {
        ++stats_.invalid_packets;
        log_.debug("invalid_packet", {kv("reason", to_string(d.error)), kv("size", bytes.size())});
        return;
    }
    handle(from, *d.packet, now_us);
}

// A connection-oriented backend detected that the peer is gone: start the grace period now instead of waiting
// for our own timeout (review #3 answer 2: the transport owns liveness when it can detect it).
void Host::on_transport_disconnect(PeerId peer, std::uint64_t now_us) {
    for (auto& s : slots_) {
        if (!s.connected || s.peer != peer) continue;
        s.connected = false;
        s.disconnected_at_us = now_us;
        s.pending.clear();
        ++stats_.timeouts;
        ++stats_.transport_disconnects;
        log_.info("player_transport_lost", {kv("player", s.player_id), kv("grace_ms", cfg_.reconnect_grace_us / 1000)});
    }
}

void Host::handle(PeerId from, const Packet& p, std::uint64_t now_us) {
    if (const auto* req = std::get_if<ConnectRequest>(&p.message)) {
        on_connect(from, *req, now_us);
        return;
    }
    Slot* s = find_by_peer(from);
    if (!s || p.header.session != session_) {
        // A repeated Disconnect after we already released the slot (our first DisconnectAck was lost): confirm again.
        // Only for this session and only as a reply to that peer, so it cannot be used for reflection.
        if (!s && p.header.session == session_ && std::holds_alternative<Disconnect>(p.message)) {
            ack_leave(from, now_us);
            ++stats_.leave_acks_repeated;
            return;
        }
        ++stats_.unknown_peer;
        if (p.header.session == session_) notify_timed_out(from, now_us);
        return;
    }
    // Only host-bound message types prove the peer is alive (review FB-1-02).
    const bool host_bound = std::holds_alternative<Heartbeat>(p.message) || std::holds_alternative<InputBatch>(p.message) ||
                            std::holds_alternative<Disconnect>(p.message) || std::holds_alternative<EventAck>(p.message);
    if (!host_bound) {
        ++stats_.invalid_packets;
        return;
    }
    s->last_recv_us = now_us;
    if (seq_newer(p.header.sequence, s->peer_seq)) s->peer_seq = p.header.sequence;
    std::visit(overloaded{
        [&](const Heartbeat& hb) { send(*s, HeartbeatAck{hb.send_time_us}, now_us); },
        [&](const InputBatch& batch) {
            for (const auto& cmd : batch.inputs) {
                if (!seq_newer(cmd.input_seq, s->last_received_input)) {
                    ++stats_.stale_inputs;
                    continue;
                }
                s->pending.push_back(cmd);
                s->last_received_input = cmd.input_seq;
                if (s->pending.size() > kMaxPendingInputs) s->pending.pop_front();
            }
        },
        [&](const Disconnect& d) {
            log_.info("player_left", {kv("player", s->player_id), kv("reason", static_cast<int>(d.reason))});
            ack_leave(from, now_us);  // FB-10-04: confirm before the slot (and its transport peer) goes away
            ++stats_.clean_leaves;
            remove_slot(s->player_id);
        },
        [&](const EventAck& a) {  // the client APPLICATION applied everything up to this id: forget those events
            // Review #10 FB-10-01: an ack can only cover events this host actually sent in this session. Anything
            // beyond (a forged, corrupted or confused ack) is refused WHOLE, so it can never erase an event that
            // was never delivered (e.g. one queued while the player was away).
            if (a.applied_through > s->highest_sent_event) {
                ++stats_.event_acks_invalid;
                log_.warn("event_ack_invalid", {kv("player", s->player_id), kv("applied_through", a.applied_through),
                                                kv("highest_sent", s->highest_sent_event)});
                return;
            }
            while (!s->events.empty() && s->events.front().event.event_id <= a.applied_through) {
                s->events.pop_front();
                ++stats_.events_applied;
            }
        },
        [&](const auto&) {},  // unreachable: filtered by host_bound above
    }, p.message);
}

// A peer whose slot is in its grace period may not know it timed out (KI-12: e.g. buffered snapshots refreshed
// its liveness after a stall). Tell it, so it reconnects with its token now instead of after its own timeout.
// Only peers of grace slots get a reply (no reflection to arbitrary sources), at most every 250 ms.
void Host::notify_timed_out(PeerId from, std::uint64_t now_us) {
    constexpr std::uint64_t kNoticeIntervalUs = 250'000;
    for (auto& s : slots_) {
        if (s.connected || s.peer != from) continue;
        if (s.last_notice_us != 0 && now_us - s.last_notice_us < kNoticeIntervalUs) return;
        s.last_notice_us = now_us;
        ++stats_.timeout_notices;
        send(s, Disconnect{DisconnectReason::Timeout}, now_us);
        return;
    }
}

void Host::on_connect(PeerId from, const ConnectRequest& req, std::uint64_t now_us) {
    if (req.reconnect_token != 0) {
        for (auto& s : slots_) {
            if (s.token != req.reconnect_token) continue;
            const bool was_connected = s.connected;
            if (Slot* other = find_by_peer(from); other && other != &s) remove_slot(other->player_id, false);
            Slot* slot = nullptr;  // remove_slot may have moved elements
            for (auto& t : slots_)
                if (t.token == req.reconnect_token) slot = &t;
            if (slot->peer != from && slot->peer.valid()) transport_->close_peer(slot->peer);  // migrate identity
            slot->peer = from;
            slot->connected = true;
            slot->nonce = req.client_nonce;
            slot->last_recv_us = now_us;
            if (!was_connected) {
                ++stats_.reconnects;
                log_.info("player_reconnected", {kv("player", slot->player_id), kv("entity", slot->entity_id)});
            }
            // New connection: every unapplied event goes out again on it, in order (FB-9-02). The client drops what
            // it already holds; its application acks only what it applied.
            for (auto& e : slot->events) {
                if (e.last_sent_us != 0) ++stats_.events_replayed;
                e.last_sent_us = 0;
            }
            accept(*slot, now_us);
            return;
        }
        log_.info("unknown_reconnect_token");  // fall through: treat as a new player
    }
    if (Slot* s = find_by_peer(from)) {
        if (s->nonce == req.client_nonce) {  // retransmitted request: the peer is alive
            s->last_recv_us = now_us;
            accept(*s, now_us);
            return;
        }
        remove_slot(s->player_id, false);  // a new client instance reused the same transport peer
    }
    if (next_player_id_ == 0 || !world_.can_spawn()) {  // 16-bit id space exhausted: never wrap to 0 or reuse ids
        log_.warn("join_rejected", {kv("reason", "ids_exhausted")});
        send_unconnected(from, ConnectReject{RejectReason::ServerFull, kProtocolVersion}, now_us);
        return;
    }
    if (slots_.size() >= cfg_.max_players) {
        ++stats_.rejected_full;
        log_.info("join_rejected", {kv("reason", "server_full")});
        send_unconnected(from, ConnectReject{RejectReason::ServerFull, kProtocolVersion}, now_us);
        return;
    }
    Slot s;
    s.player_id = next_player_id_++;
    s.entity_id = world_.spawn(s.player_id).id;
    s.peer = from;
    s.connected = true;
    s.token = random_nonzero();
    s.nonce = req.client_nonce;
    s.last_recv_us = now_us;
    slots_.push_back(s);
    ++stats_.joins;
    log_.info("player_joined", {kv("player", s.player_id), kv("entity", s.entity_id), kv("connected", connected_count())});
    accept(slots_.back(), now_us);
}

void Host::accept(Slot& s, std::uint64_t now_us) {
    send(s, ConnectAccept{s.nonce, s.player_id, s.entity_id, s.token, cfg_.tick_rate_hz, world_.tick()}, now_us);
}

void Host::remove_slot(std::uint16_t player_id, bool close_transport_peer) {
    for (const auto& s : slots_)
        if (s.player_id == player_id && !s.events.empty()) {
            stats_.events_dropped_with_slot += s.events.size();  // the player is gone for good: counted, never silent
            log_.warn("events_dropped_with_player", {kv("player", player_id), kv("events", s.events.size())});
        }
    auto it = std::find_if(slots_.begin(), slots_.end(), [&](const Slot& s) { return s.player_id == player_id; });
    if (it == slots_.end()) return;
    if (close_transport_peer && it->peer.valid()) transport_->close_peer(it->peer);
    world_.despawn(it->entity_id);
    slots_.erase(it);
}

void Host::step() {
    const float dt = 1.0f / static_cast<float>(cfg_.tick_rate_hz);
    for (auto& s : slots_) {
        sim::Entity* e = world_.find(s.entity_id);
        if (!e) continue;
        if (!s.pending.empty()) {
            // Exactly one simulation timestep per tick (review FB-1-01). A jitter backlog beyond the
            // target is shed oldest-first instead of simulated, so input delay stays bounded and entity
            // time never runs ahead of world time. Shed inputs count as consumed: the client stops
            // replaying them and reconciliation absorbs the lost movement.
            while (s.pending.size() > kTargetInputBacklog + 1) {
                s.last_processed_input = s.pending.front().input_seq;
                s.pending.pop_front();
                ++stats_.shed_inputs;
            }
            const InputCmd cmd = s.pending.front();
            s.pending.pop_front();
            s.last_processed_input = cmd.input_seq;
            sim::apply_input(*e, cmd.move_x, cmd.move_z, dt);
        } else {
            sim::apply_input(*e, 0, 0, dt);  // input starvation: stand still rather than guess
        }
    }
    world_.advance_tick();
}

void Host::broadcast(std::uint64_t now_us) {
    Snapshot snap;
    snap.server_tick = world_.tick();
    for (const auto& e : world_.entities()) {
        if (snap.entities.size() == kMaxEntitiesPerSnapshot) break;
        snap.entities.push_back({e.id, e.owner, e.t, e.vx, e.vz});
    }
    for (auto& s : slots_) {
        if (!s.connected) continue;
        snap.last_input_seq = s.last_processed_input;
        send(s, snap, now_us);
    }
}

void Host::check_timeouts(std::uint64_t now_us) {
    std::vector<std::uint16_t> expired;
    for (auto& s : slots_) {
        if (s.connected && now_us - s.last_recv_us > cfg_.timeout_us) {
            s.connected = false;
            s.disconnected_at_us = now_us;
            s.pending.clear();
            ++stats_.timeouts;
            log_.info("player_timeout", {kv("player", s.player_id), kv("grace_ms", cfg_.reconnect_grace_us / 1000)});
        } else if (!s.connected && now_us - s.disconnected_at_us > cfg_.reconnect_grace_us) {
            expired.push_back(s.player_id);
        }
    }
    for (auto id : expired) {
        log_.info("player_expired", {kv("player", id)});
        remove_slot(id);
    }
}

SendResult Host::transmit(PeerId to, const std::vector<std::uint8_t>& bytes, std::uint64_t now_us, Delivery delivery) {
    ++stats_.packets_out;
    stats_.bytes_out += bytes.size();  // offered load, before simulated loss
    const auto r = transport_->send(to, bytes, delivery, now_us);
    if (r != SendResult::Sent) {
        ++stats_.send_errors;  // surfaced, never silent (review FB-4-04)
        log_.debug("send_failed", {kv("result", to_string(r))});
    }
    return r;
}

SendResult Host::send(Slot& s, const Message& m, std::uint64_t now_us, Delivery delivery) {
    Header h;
    h.session = session_;
    h.sequence = ++s.send_seq;
    h.ack = s.peer_seq;
    auto bytes = encode(h, m);
    if (bytes.empty()) {
        log_.error("encode_failed", {kv("type", static_cast<int>(type_of(m)))});
        return SendResult::Failed;
    }
    return transmit(s.peer, bytes, now_us, delivery);
}

// Two unreliable copies plus a reliable one where offered; sent before the slot and its transport peer are released.
void Host::ack_leave(PeerId to, std::uint64_t now_us) {
    Header h;
    h.session = session_;
    const auto bytes = encode(h, DisconnectAck{});
    for (int i = 0; i < 2; ++i) transmit(to, bytes, now_us, Delivery::Unreliable);
    if (transport_->capabilities().reliable_ordered) transmit(to, bytes, now_us, Delivery::ReliableOrdered);
    transport_->flush(now_us);
}

void Host::send_unconnected(PeerId to, const Message& m, std::uint64_t now_us) {
    auto bytes = encode(Header{}, m);
    if (bytes.empty()) return;
    transmit(to, bytes, now_us, Delivery::Unreliable);
}

void Host::shutdown() {
    if (!running_) return;
    const bool reliable = transport_->capabilities().reliable_ordered;
    for (auto& s : slots_)
        if (s.connected) {
            if (reliable) send(s, Disconnect{DisconnectReason::HostShutdown}, 0, Delivery::ReliableOrdered);
            else
                for (int i = 0; i < kDisconnectRepeats; ++i) send(s, Disconnect{DisconnectReason::HostShutdown}, 0);
        }
    transport_->flush_all();
    const auto& ts = transport_->stats();
    transport_->close();
    running_ = false;
    // Telemetry for soak analysis (tools/soak/analyze.py): tick drift = world ticks vs wall-clock uptime.
    log_.info("host_stopped", {kv("packets_in", stats_.packets_in), kv("packets_out", stats_.packets_out),
                               kv("invalid", stats_.invalid_packets), kv("bytes_in", stats_.bytes_in),
                               kv("bytes_out", stats_.bytes_out), kv("world_tick", world_.tick()),
                               kv("uptime_us", last_update_us_ - first_update_us_), kv("tick_rate_hz", cfg_.tick_rate_hz),
                               kv("skipped_ticks", stats_.skipped_ticks), kv("shed_inputs", stats_.shed_inputs),
                               kv("stale_inputs", stats_.stale_inputs), kv("joins", stats_.joins),
                               kv("reconnects", stats_.reconnects), kv("timeouts", stats_.timeouts),
                               kv("timeout_notices", stats_.timeout_notices),
                               kv("unknown_peer", stats_.unknown_peer), kv("send_errors", stats_.send_errors),
                               kv("transport", to_string(cfg_.transport)),
                               kv("transport_accepted_then_failed", ts.accepted_then_failed),
                               kv("transport_discarded_on_close", ts.discarded_on_close),
                               kv("transport_closed_with_unacked_reliable", ts.closed_with_unacked_reliable),
                               kv("transport_impairment_refused", ts.impairment_refused),
                               kv("events_queued", stats_.events_queued), kv("events_applied", stats_.events_applied),
                               kv("events_retransmitted", stats_.events_retransmitted), kv("events_replayed", stats_.events_replayed),
                               kv("events_dropped_with_slot", stats_.events_dropped_with_slot),
                               kv("events_send_refused", stats_.events_send_refused), kv("event_acks_invalid", stats_.event_acks_invalid),
                               kv("clean_leaves", stats_.clean_leaves), kv("leave_acks_repeated", stats_.leave_acks_repeated)});
    if (const auto* ss = transport_ ? net::secure_stats(*transport_) : nullptr)  // secure mode (FB-8-03): no secrets
        log_.info("secure_stats", {kv("authenticated", ss->authenticated), kv("pending_opened", ss->pending_opened),
                                   kv("pending_failed", ss->pending_failed), kv("pending_refused_cap", ss->pending_refused_cap),
                                   kv("dropped_not_dtls", ss->dropped_not_dtls), kv("idle_timeouts", ss->idle_timeouts),
                                   kv("reliable_retransmits", ss->reliable_retransmits),
                                   kv("pending_replaced", ss->pending_replaced), kv("hello_verify_sent", ss->hello_verify_sent),
                                   kv("hello_rate_limited", ss->hello_rate_limited), kv("promotions_replacing", ss->promotions_replacing),
                                   kv("rejected_established", ss->rejected_established), kv("handshakes_done", ss->handshakes_done),
                                   kv("handshake_retransmits", ss->handshake_retransmits), kv("handshake_ms_max", ss->handshake_ms_max)});
}

}  // namespace mordor::host
