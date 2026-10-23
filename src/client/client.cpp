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
    }
    return "?";
}

Client::Client(ClientConfig cfg, Logger& log)
    : cfg_(cfg), log_(log), sender_(socket_, cfg.impairment), rng_(std::random_device{}()) {}

bool Client::start(std::string* error) {
    if (!cfg_.impairment.valid()) {
        if (error) *error = "invalid impairment configuration";
        return false;
    }
    // Bind to loopback when talking to a loopback host so tests never open a LAN-facing port.
    const Endpoint bind_to = cfg_.host.is_loopback() ? Endpoint::loopback(0) : Endpoint{0, 0};
    return socket_.open(bind_to, error);
}

void Client::set_state(ClientState s, std::string_view why) {
    if (s == state_) return;
    log_.info("state_change", {kv("from", to_string(state_)), kv("to", to_string(s)), kv("why", why)});
    state_ = s;
}

void Client::connect(std::uint64_t now_us) {
    do nonce_ = rng_(); while (nonce_ == 0);
    token_ = 0;
    session_ = 0;
    attempted_ = false;
    phase_started_us_ = now_us;
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
    if (!socket_.is_open()) return;
    receive(now_us);
    switch (state_) {
        case ClientState::Connecting:
        case ClientState::Reconnecting: {
            const bool reconnecting = state_ == ClientState::Reconnecting;
            const auto window = reconnecting ? cfg_.reconnect_window_us : cfg_.connect_timeout_us;
            if (now_us - phase_started_us_ > window) {
                set_state(ClientState::Failed, reconnecting ? "reconnect_window_expired" : "connect_timeout");
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
            }
            break;
        }
        default: break;
    }
    sender_.flush(now_us);
}

void Client::receive(std::uint64_t now_us) {
    std::array<std::uint8_t, 2048> buf{};
    for (int i = 0; i < kMaxPacketsPerUpdate; ++i) {
        Endpoint from;
        const auto r = socket_.recv_from(from, buf);
        if (r.status == RecvStatus::Empty) break;
        if (r.status == RecvStatus::Error) {
            log_.warn("recv_error", {kv("os_error", r.os_error)});
            break;
        }
        if (from != cfg_.host) {
            ++stats_.foreign_packets;
            continue;
        }
        ++stats_.packets_in;
        if (r.status == RecvStatus::Oversized) {
            ++stats_.invalid_packets;
            continue;
        }
        stats_.bytes_in += r.size;
        const auto d = decode({buf.data(), r.size});
        if (d.error != DecodeError::None) {
            ++stats_.invalid_packets;
            log_.debug("invalid_packet", {kv("reason", to_string(d.error))});
            continue;
        }
        handle(*d.packet, now_us);
    }
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
        if (reconnect) {
            ++stats_.reconnects;
            stats_.last_reconnect_ms = (now_us - reconnect_started_us_) / 1000;
            stats_.max_reconnect_ms = std::max(stats_.max_reconnect_ms, stats_.last_reconnect_ms);
        }
        log_.info(reconnect ? "reconnected" : "connected",
                  {kv("player", player_id_), kv("entity", entity_id_), kv("session", session_), kv("same_session", same_session)});
        set_state(ClientState::Connected, "connect_accept");
        return;
    }
    if (state_ != ClientState::Connected || p.header.session != session_) {
        ++stats_.foreign_packets;
        return;
    }
    // Only client-bound message types prove the host is alive (review FB-1-02).
    const bool client_bound = std::holds_alternative<Snapshot>(p.message) || std::holds_alternative<HeartbeatAck>(p.message) ||
                              std::holds_alternative<Disconnect>(p.message);
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
        [&](const auto&) {},  // unreachable: filtered by client_bound above
    }, p.message);
}

void Client::begin_reconnect(std::uint64_t now_us, std::string_view why) {
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

void Client::send(const Message& m, std::uint64_t now_us) {
    Header h;
    h.version = cfg_.protocol_version;
    h.session = session_;
    h.sequence = ++send_seq_;
    h.ack = peer_seq_;
    auto bytes = encode(h, m);
    if (bytes.empty()) {
        log_.error("encode_failed", {kv("type", static_cast<int>(type_of(m)))});
        return;
    }
    ++stats_.packets_out;
    stats_.bytes_out += bytes.size();  // offered load, before simulated loss
    sender_.send(cfg_.host, std::move(bytes), now_us);
}

void Client::disconnect() {
    // Unreliable channel: send the notice a few times; the host ignores repeats. Timeout remains the fallback.
    if (state_ == ClientState::Connected)
        for (int i = 0; i < kDisconnectRepeats; ++i) send(Disconnect{DisconnectReason::ClientQuit}, 0);
    sender_.flush_all();
    disconnect_reason_ = DisconnectReason::ClientQuit;
    set_state(ClientState::Disconnected, "local_disconnect");
}

}  // namespace mordor::client
