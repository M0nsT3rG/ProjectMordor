#include "client/client.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace mordor::client {

using namespace mordor::net;

namespace {
constexpr std::size_t kMaxPendingInputs = 64;
constexpr int kMaxPacketsPerUpdate = 512;
constexpr float kCorrectionEpsilon = 0.01f;
constexpr int kDisconnectRepeats = 3;
constexpr std::size_t kMaxRttSamples = 1'000'000;  // ~70 h at 4 heartbeats/s; bounds memory

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
}  // namespace

const char* to_string(ClientState s) {
    switch (s) {
        case ClientState::Idle: return "idle";
        case ClientState::Connecting: return "connecting";
        case ClientState::Connected: return "connected";
        case ClientState::Reconnecting: return "reconnecting";
        case ClientState::Rejected: return "rejected";
        case ClientState::Disconnected: return "disconnected";
        case ClientState::Failed: return "failed";
        case ClientState::Leaving: return "leaving";
    }
    return "?";
}

Client::Client(ClientConfig cfg, Logger& log)
    : cfg_(cfg), log_(log), rng_(std::random_device{}()) {}

bool Client::start(std::string* error) {
    if (!cfg_.impairment.valid()) {
        if (error) *error = "invalid impairment configuration";
        return false;
    }
    if (cfg_.connect_deadline_us < kMinConnectDeadlineUs || cfg_.connect_deadline_us > kMaxConnectDeadlineUs) {
        if (error) *error = "connect_deadline_us outside [1 s, 120 s]";
        return false;
    }
    // Bind to loopback when talking to a loopback host so tests never open a LAN-facing port.
    const Endpoint bind_to = cfg_.host.is_loopback() ? Endpoint::loopback(0) : Endpoint{0, 0};
    transport_ = make_transport(cfg_.transport, cfg_.impairment, error);
    if (transport_ && cfg_.secure) transport_ = make_secure_transport(std::move(transport_), *cfg_.secure, error);
    if (!transport_ || !transport_->open(bind_to, false, error)) {
        transport_.reset();
        return false;
    }
    return true;
}

void Client::set_state(ClientState s, std::string_view why) {
    if (s == state_) return;
    log_.info("state_change", {kv("from", to_string(state_)), kv("to", to_string(s)), kv("why", why)});
    state_ = s;
    state_reason_ = why;
}

void Client::connect(std::uint64_t now_us) {
    if (transport_) server_peer_ = transport_->connect(cfg_.host);
    do nonce_ = rng_(); while (nonce_ == 0);
    token_ = 0;
    session_ = 0;
    attempted_ = false;
    phase_started_us_ = connect_started_us_ = now_us;
    reject_reason_.reset();
    disconnect_reason_.reset();
    set_state(ClientState::Connecting, "connect_called");
}

std::optional<double> Client::rtt_ms() const {
    if (!rtt_us_) return std::nullopt;
    return *rtt_us_ / 1000.0;
}

std::optional<Transform> Client::predicted_self() const {
    if (!predicted_) return std::nullopt;
    return predicted_->t;
}

std::optional<Transform> Client::authoritative_self() const {
    if (!authoritative_) return std::nullopt;
    return authoritative_->t;
}

std::vector<std::uint16_t> Client::remote_entities() const {
    std::vector<std::uint16_t> ids;
    for (const auto& [id, buf] : remotes_) ids.push_back(id);
    return ids;
}

std::optional<Transform> Client::remote_view(std::uint16_t entity_id, std::uint64_t now_us) const {
    auto it = remotes_.find(entity_id);
    if (it == remotes_.end() || !latest_tick_) return std::nullopt;
    const double tick_us = 1e6 / tick_rate_hz_;
    const double since = std::min(1.0, static_cast<double>(now_us - last_snapshot_us_) / tick_us);
    return it->second.sample(static_cast<double>(*latest_tick_) + since - cfg_.interp_delay_ticks);
}

void Client::update(std::uint64_t now_us) {
    if (!transport_ || !transport_->is_open()) return;
    receive(now_us);
    switch (state_) {
        case ClientState::Connecting:
        case ClientState::Reconnecting: {
            const bool reconnecting = state_ == ClientState::Reconnecting;
            const auto window = reconnecting ? cfg_.reconnect_window_us : cfg_.connect_timeout_us;
            // D-014: on a connection-oriented transport the transport owns connection establishment (its own
            // initial timeout); our connect window starts once it reports Connected, so the two never race.
            const bool waiting_for_transport =
                !reconnecting && transport_->capabilities().reports_disconnects && !transport_up_;
            if (!waiting_for_transport && now_us - phase_started_us_ > window) {
                set_state(ClientState::Failed, reconnecting ? "reconnect_window_expired" : "connect_timeout");
                break;
            }
            // The overall bound does not trust the transport to ever report failure (FB-5-02).
            if (!reconnecting && now_us - connect_started_us_ > cfg_.connect_deadline_us) {
                set_state(ClientState::Failed, "connect_deadline");
                break;
            }
            const auto retry = reconnecting ? cfg_.reconnect_retry_us : cfg_.connect_retry_us;
            if (!attempted_ || now_us - last_attempt_us_ >= retry) {
                attempted_ = true;
                last_attempt_us_ = now_us;
                send(ConnectRequest{nonce_, token_}, now_us);
            }
            break;
        }
        case ClientState::Connected: {
            if (now_us - last_recv_us_ > cfg_.timeout_us) {
                log_.warn("connection_lost", {kv("silence_ms", (now_us - last_recv_us_) / 1000)});
                begin_reconnect(now_us, "timeout");
                break;
            }
            const std::uint64_t tick_us = 1'000'000 / tick_rate_hz_;
            if (now_us > next_input_us_ + 5 * tick_us) next_input_us_ = now_us;  // same stall policy as the host
            while (now_us >= next_input_us_) {
                produce_input(now_us);
                next_input_us_ += tick_us;
            }
            if (now_us - last_heartbeat_us_ >= cfg_.heartbeat_interval_us) {
                last_heartbeat_us_ = now_us;
                send(Heartbeat{now_us}, now_us);
                // Re-confirm what the application applied: covers a lost ack (unreliable transport) and tells a
                // host that replayed after a reconnect what it can forget (FB-9-02).
                if (applied_through_ > 0) send(EventAck{applied_through_}, now_us, ack_delivery());
            }
            break;
        }
        case ClientState::Leaving: {
            if (now_us - leave_started_us_ >= cfg_.leave_timeout_us) {
                stats_.leave_ms = (now_us - leave_started_us_) / 1000;
                log_.warn("leave_unconfirmed", {kv("disconnects_sent", stats_.leave_disconnects_sent),
                                                kv("disconnects_refused", stats_.leave_disconnects_refused)});
                disconnect_reason_ = DisconnectReason::ClientQuit;
                set_state(ClientState::Disconnected, "leave_timeout");
                break;
            }
            if (now_us - last_leave_send_us_ >= cfg_.leave_resend_us) {
                last_leave_send_us_ = now_us;
                ++stats_.leave_disconnects_sent;
                if (!send(Disconnect{DisconnectReason::ClientQuit}, now_us)) ++stats_.leave_disconnects_refused;
            }
            break;
        }
        default: break;
    }
    transport_->flush(now_us);
}

void Client::receive(std::uint64_t now_us) {
    events_.clear();
    transport_->poll(now_us, events_);
    for (auto& e : events_) {
        const bool from_host = server_peer_ && e.peer == *server_peer_;
        switch (e.kind) {
            case TransportEvent::Kind::Connected:
                if (from_host) {
                    log_.info("transport_connected", {kv("state", to_string(state_))});
                    transport_up_ = true;
                    if (state_ == ClientState::Connecting) phase_started_us_ = now_us;  // D-014: full window from here
                    // KI-35: requests sent before the transport was up were refused; send one now instead of
                    // waiting out the retry interval (up to 500 ms when reconnecting).
                    if (state_ == ClientState::Connecting || state_ == ClientState::Reconnecting) attempted_ = false;
                }
                break;
            case TransportEvent::Kind::Disconnected:
                if (from_host && state_ == ClientState::Connecting && !transport_up_) {
                    transport_up_ = false;  // the transport itself gave up on the handshake: that is final (D-014)
                    set_state(ClientState::Failed, "transport_handshake_failed");
                    break;
                }
                if (from_host) {
                    transport_up_ = false;
                    log_.info("transport_disconnected", {kv("state", to_string(state_))});
                }
                if (from_host && state_ == ClientState::Leaving) {
                    // The host released us before its DisconnectAck reached us (or never sent one): we are gone
                    // either way; never reconnect while leaving. leave_confirmed stays false.
                    stats_.leave_ms = (now_us - leave_started_us_) / 1000;
                    disconnect_reason_ = DisconnectReason::ClientQuit;
                    set_state(ClientState::Disconnected, "leave_transport_closed");
                    break;
                }
                // A connection-oriented backend lost the host: always open a new transport connection, in ANY state.
                // (If our own timeout already moved us to Reconnecting, ignoring this left us sending reconnect
                // requests on a dead PeerId forever; found by the first GNS run, EXP-016.)
                if (from_host) {
                    ++stats_.transport_disconnects;
                    server_peer_ = transport_->connect(cfg_.host);
                    if (state_ == ClientState::Connected) begin_reconnect(now_us, "transport_disconnected");
                }
                break;
            case TransportEvent::Kind::DroppedInbound:
                if (!from_host) {
                    ++stats_.foreign_packets;
                    break;
                }
                ++stats_.packets_in;
                ++stats_.invalid_packets;
                break;
            case TransportEvent::Kind::Message: {
                if (!from_host) {
                    ++stats_.foreign_packets;
                    break;
                }
                ++stats_.packets_in;
                stats_.bytes_in += e.bytes.size();
                const auto d = decode(e.bytes);
                if (d.error != DecodeError::None) {
                    ++stats_.invalid_packets;
                    log_.debug("invalid_packet", {kv("reason", to_string(d.error))});
                    break;
                }
                handle(*d.packet, now_us);
                break;
            }
        }
    }
    events_.clear();
}

void Client::handle(const Packet& p, std::uint64_t now_us) {
    if (const auto* rej = std::get_if<ConnectReject>(&p.message)) {
        if (state_ == ClientState::Connecting || state_ == ClientState::Reconnecting) {
            reject_reason_ = rej->reason;
            log_.warn("rejected", {kv("reason", static_cast<int>(rej->reason)), kv("host_version", rej->host_version)});
            set_state(ClientState::Rejected, "connect_reject");
        }
        return;
    }
    if (const auto* acc = std::get_if<ConnectAccept>(&p.message)) {
        if (state_ != ClientState::Connecting && state_ != ClientState::Reconnecting) return;  // duplicate accept
        if (acc->client_nonce != nonce_) return;  // answer to an earlier attempt
        const bool reconnect = state_ == ClientState::Reconnecting;
        const bool same_session = reconnect && p.header.session == session_;
        session_ = p.header.session;
        player_id_ = acc->player_id;
        entity_id_ = acc->entity_id;
        token_ = acc->reconnect_token;
        tick_rate_hz_ = acc->tick_rate_hz;
        last_recv_us_ = next_input_us_ = last_heartbeat_us_ = now_us;
        peer_seq_ = accept_seq_ = p.header.sequence;
        pending_inputs_.clear();
        predicted_.reset();
        authoritative_.reset();
        remotes_.clear();
        if (!same_session) latest_tick_.reset();
        if (event_session_ != session_) {  // a new host session (e.g. host restart): its event ids start again at 1
            stats_.events_discarded_new_session += event_inbox_.size();
            event_inbox_.clear();
            event_session_ = session_;
            next_event_ = 1;
            applied_through_ = 0;
        }
        if (reconnect) {
            ++stats_.reconnects;
            stats_.last_reconnect_ms = (now_us - reconnect_started_us_) / 1000;
            stats_.max_reconnect_ms = std::max(stats_.max_reconnect_ms, stats_.last_reconnect_ms);
            awaiting_first_snapshot_ = true;
        }
        log_.info(reconnect ? "reconnected" : "connected",
                  {kv("player", player_id_), kv("entity", entity_id_), kv("session", session_), kv("same_session", same_session),
                   kv("reconnect_ms", reconnect ? stats_.last_reconnect_ms : 0)});
        set_state(ClientState::Connected, "connect_accept");
        return;
    }
    if (state_ == ClientState::Leaving && p.header.session == session_ && std::holds_alternative<DisconnectAck>(p.message)) {
        stats_.leave_confirmed = true;
        stats_.leave_ms = (now_us - leave_started_us_) / 1000;
        log_.info("leave_confirmed", {kv("leave_ms", stats_.leave_ms), kv("disconnects_sent", stats_.leave_disconnects_sent)});
        disconnect_reason_ = DisconnectReason::ClientQuit;
        set_state(ClientState::Disconnected, "leave_acked");
        return;
    }
    if (state_ != ClientState::Connected || p.header.session != session_) {
        ++stats_.foreign_packets;
        return;
    }
    // Only client-bound message types prove the host is alive (review FB-1-02).
    const bool client_bound = std::holds_alternative<Snapshot>(p.message) || std::holds_alternative<HeartbeatAck>(p.message) ||
                              std::holds_alternative<Disconnect>(p.message) || std::holds_alternative<GameEvent>(p.message);
    if (!client_bound) {
        ++stats_.invalid_packets;
        return;
    }
    // A Disconnect sent before the accept that established this connection is stale: a delayed or replayed
    // timeout notice from the previous grace period must not tear down the fresh connection (review FB-2-02).
    // Not a security control: without authentication a forged packet can still claim a newer sequence.
    if (std::holds_alternative<Disconnect>(p.message) && !seq_newer(p.header.sequence, accept_seq_)) {
        ++stats_.stale_notices;
        return;
    }
    last_recv_us_ = now_us;
    if (seq_newer(p.header.sequence, peer_seq_)) peer_seq_ = p.header.sequence;
    std::visit(overloaded{
        [&](const Snapshot& s) { on_snapshot(s, now_us); },
        [&](const HeartbeatAck& a) {
            if (a.echo_time_us > now_us) return;
            const double sample = static_cast<double>(now_us - a.echo_time_us);
            rtt_us_ = rtt_us_ ? *rtt_us_ * 0.875 + sample * 0.125 : sample;
            if (rtt_samples_ms_.size() < kMaxRttSamples) rtt_samples_ms_.push_back(static_cast<float>(sample / 1000.0));
        },
        [&](const Disconnect& d) {
            if (d.reason == DisconnectReason::Timeout) {  // host timed us out but keeps our slot: reclaim it now (KI-12)
                begin_reconnect(now_us, "host_timeout_notice");
                return;
            }
            disconnect_reason_ = d.reason;
            set_state(ClientState::Disconnected, "host_disconnect");
        },
        [&](const GameEvent& ev) {
            if (ev.event_id < next_event_) {  // already delivered (retransmit or replay): drop, re-confirm what is applied
                ++stats_.events_duplicate;
                if (applied_through_ > 0) send(EventAck{applied_through_}, now_us, ack_delivery());
                return;
            }
            if (ev.event_id != next_event_) {  // a gap: the host resends in order, never deliver out of order
                ++stats_.events_out_of_order;
                return;
            }
            ++next_event_;
            ++stats_.events_received;
            event_inbox_.push_back({ev.event_id, ev.kind, ev.payload});
        },
        [&](const auto&) {},  // unreachable: filtered by client_bound above
    }, p.message);
}

std::vector<ReceivedEvent> Client::take_events() { return std::exchange(event_inbox_, {}); }

void Client::ack_events_applied(std::uint32_t through_id, std::uint64_t now_us) {
    // Only what has been delivered can have been applied; acks never move backwards.
    if (through_id >= next_event_ || through_id <= applied_through_) return;
    applied_through_ = through_id;
    if (state_ == ClientState::Connected) send(EventAck{applied_through_}, now_us, ack_delivery());
}

// EventAck is cumulative and idempotent, and re-sent with every heartbeat: Unreliable. Sending it ReliableOrdered
// put a stream of acks in front of the final Disconnect, which then waited behind a lost ack until the client's close
// drain ended (head-of-line blocking; the secure moderate GNS soak timed the player out, EXP-022).
Delivery Client::ack_delivery() const { return Delivery::Unreliable; }

void Client::begin_reconnect(std::uint64_t now_us, std::string_view why) {
    // Connection-oriented transport: a reconnect always uses a NEW transport connection (review #3 answer 2).
    // Reusing the old one fails when the host restarted: the new host does not know that connection (EXP-016).
    if (transport_ && transport_->capabilities().reports_disconnects && server_peer_) {
        transport_->close_peer(*server_peer_);
        server_peer_ = transport_->connect(cfg_.host);
        transport_up_ = false;
    }
    phase_started_us_ = reconnect_started_us_ = now_us;
    attempted_ = false;
    do nonce_ = rng_(); while (nonce_ == 0);  // new attempt: accepts for the old nonce are now stale
    set_state(ClientState::Reconnecting, why);
}

void Client::on_snapshot(const Snapshot& s, std::uint64_t now_us) {
    if (latest_tick_ && !seq_newer(s.server_tick, *latest_tick_)) {
        ++stats_.stale_snapshots;  // duplicated or reordered: never roll back to older state
        return;
    }
    latest_tick_ = s.server_tick;
    last_snapshot_us_ = now_us;
    if (awaiting_first_snapshot_) {  // KI-35 timeline: when play actually resumed after a reconnect
        awaiting_first_snapshot_ = false;
        log_.info("first_snapshot_after_reconnect", {kv("since_reconnect_start_ms", (now_us - reconnect_started_us_) / 1000)});
    }
    ++stats_.snapshots_applied;
    std::set<std::uint16_t> seen;
    for (const auto& e : s.entities) {
        if (e.entity_id == entity_id_) {
            sim::Entity auth{e.entity_id, e.owner_player, e.transform, e.vel_x, e.vel_z};
            authoritative_ = auth;
            std::erase_if(pending_inputs_, [&](const InputCmd& c) { return !seq_newer(c.input_seq, s.last_input_seq); });
            if (!cfg_.prediction) {
                predicted_ = auth;
                continue;
            }
            // Reconcile: start from authority, replay inputs the host has not processed yet.
            const float dt = 1.0f / static_cast<float>(tick_rate_hz_);
            for (const auto& c : pending_inputs_) sim::apply_input(auth, c.move_x, c.move_z, dt);
            if (predicted_) {
                const float err = std::hypot(predicted_->t.x - auth.t.x, predicted_->t.z - auth.t.z);
                if (err > kCorrectionEpsilon) {
                    ++stats_.corrections;
                    if (correction_samples_.size() < kMaxRttSamples) correction_samples_.push_back(err);
                    stats_.max_correction = std::max(stats_.max_correction, err);
                }
            }
            predicted_ = auth;
        } else {
            seen.insert(e.entity_id);
            remotes_[e.entity_id].push(s.server_tick, e.transform);
        }
    }
    std::erase_if(remotes_, [&](const auto& kv_) { return !seen.contains(kv_.first); });
}

void Client::produce_input(std::uint64_t now_us) {
    InputCmd cmd{++input_seq_, std::clamp(move_x_, -1.0f, 1.0f), std::clamp(move_z_, -1.0f, 1.0f)};
    pending_inputs_.push_back(cmd);
    if (pending_inputs_.size() > kMaxPendingInputs) pending_inputs_.pop_front();
    if (cfg_.prediction && predicted_)
        sim::apply_input(*predicted_, cmd.move_x, cmd.move_z, 1.0f / static_cast<float>(tick_rate_hz_));
    InputBatch batch;
    const std::size_t n = std::min(pending_inputs_.size(), kMaxInputsPerPacket);
    batch.inputs.assign(pending_inputs_.end() - static_cast<std::ptrdiff_t>(n), pending_inputs_.end());
    send(batch, now_us);
}

bool Client::send(const Message& m, std::uint64_t now_us, Delivery delivery) {
    Header h;
    h.version = cfg_.protocol_version;
    h.session = session_;
    h.sequence = ++send_seq_;
    h.ack = peer_seq_;
    auto bytes = encode(h, m);
    if (bytes.empty()) {
        log_.error("encode_failed", {kv("type", static_cast<int>(type_of(m)))});
        return false;
    }
    ++stats_.packets_out;
    stats_.bytes_out += bytes.size();  // offered load, before simulated loss
    const auto r = server_peer_ ? transport_->send(*server_peer_, bytes, delivery, now_us) : SendResult::NotConnected;
    if (r != SendResult::Sent) {
        ++stats_.send_errors;  // surfaced, never silent (review FB-4-04)
        log_.debug("send_failed", {kv("result", to_string(r))});
    }
    return r == SendResult::Sent;
}

void Client::leave(std::uint64_t now_us) {
    if (state_ != ClientState::Connected || !transport_) {
        disconnect();
        return;
    }
    leave_started_us_ = last_leave_send_us_ = now_us;
    auto send_disconnect = [&](Delivery d) {
        ++stats_.leave_disconnects_sent;
        if (!send(Disconnect{DisconnectReason::ClientQuit}, now_us, d)) ++stats_.leave_disconnects_refused;
    };
    if (transport_->capabilities().reliable_ordered) send_disconnect(Delivery::ReliableOrdered);
    send_disconnect(Delivery::Unreliable);
    set_state(ClientState::Leaving, "leave");
}

void Client::disconnect() {
    // Reliable transport: send the notice once, reliably. Unreliable transport: a few times; the host ignores
    // repeats. Timeout remains the fallback either way.
    if (state_ == ClientState::Connected && transport_) {
        // Best effort on both paths: reliably where offered (survives loss while the connection lingers) AND a few
        // unreliable copies (not stuck behind earlier reliable data). The host ignores repeats (EXP-022).
        if (transport_->capabilities().reliable_ordered) send(Disconnect{DisconnectReason::ClientQuit}, 0, Delivery::ReliableOrdered);
        for (int i = 0; i < kDisconnectRepeats; ++i) send(Disconnect{DisconnectReason::ClientQuit}, 0);
    }
    if (transport_) transport_->flush_all();
    disconnect_reason_ = DisconnectReason::ClientQuit;
    set_state(ClientState::Disconnected, "local_disconnect");
}

}  // namespace mordor::client
