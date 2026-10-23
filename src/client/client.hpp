#pragma once
// Client: connects to a host, sends inputs at the host tick rate, predicts its own entity,
// reconciles against authoritative snapshots and interpolates remote entities.
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "common/log.hpp"
#include "network/impairment.hpp"
#include "network/protocol.hpp"
#include "network/transport.hpp"
#include "simulation/world.hpp"

namespace mordor::client {

enum class ClientState { Idle, Connecting, Connected, Reconnecting, Rejected, Disconnected, Failed };
const char* to_string(ClientState s);

struct ClientConfig {
    net::Endpoint host = net::Endpoint::loopback(47800);
    std::uint64_t timeout_us = 3'000'000;
    std::uint64_t heartbeat_interval_us = 250'000;
    std::uint64_t connect_retry_us = 250'000;
    std::uint64_t connect_timeout_us = 5'000'000;
    std::uint64_t reconnect_retry_us = 500'000;
    std::uint64_t reconnect_window_us = 15'000'000;
    double interp_delay_ticks = 3.0;
    bool prediction = true;
    std::uint16_t protocol_version = net::kProtocolVersion;  // overridable only to test version rejection
    net::ImpairmentConfig impairment;
    net::TransportKind transport = net::TransportKind::Udp;
};

struct ClientStats {
    std::uint64_t packets_in = 0, packets_out = 0, invalid_packets = 0, foreign_packets = 0, snapshots_applied = 0,
                  stale_snapshots = 0, corrections = 0, reconnects = 0;
    std::uint64_t bytes_in = 0, bytes_out = 0;  // UDP payload bytes (headers included, IP/UDP overhead excluded)
    std::uint64_t stale_notices = 0;  // Disconnect packets older than the accept of the current connection
    std::uint64_t last_reconnect_ms = 0, max_reconnect_ms = 0;  // timeout detection -> accepted again
    float max_correction = 0;
};

class Client {
public:
    Client(ClientConfig cfg, Logger& log);
    bool start(std::string* error = nullptr);  // opens the local socket
    void connect(std::uint64_t now_us);
    void set_move(float move_x, float move_z) { move_x_ = move_x; move_z_ = move_z; }
    void update(std::uint64_t now_us);
    void disconnect();  // graceful: tells the host, then stops
    void close() {
        if (transport_) transport_->close();
    }

    ClientState state() const { return state_; }
    std::optional<net::RejectReason> reject_reason() const { return reject_reason_; }
    std::optional<net::DisconnectReason> disconnect_reason() const { return disconnect_reason_; }
    std::uint16_t player_id() const { return player_id_; }
    std::uint16_t entity_id() const { return entity_id_; }
    std::uint32_t session_id() const { return session_; }
    std::optional<std::uint32_t> latest_server_tick() const { return latest_tick_; }
    std::optional<double> rtt_ms() const;
    std::optional<net::Transform> predicted_self() const;
    std::optional<net::Transform> authoritative_self() const;
    std::optional<net::Transform> remote_view(std::uint16_t entity_id, std::uint64_t now_us) const;
    std::vector<std::uint16_t> remote_entities() const;
    const ClientStats& stats() const { return stats_; }
    const std::vector<float>& rtt_samples_ms() const { return rtt_samples_ms_; }  // raw samples for percentiles
    const std::vector<float>& correction_samples() const { return correction_samples_; }  // each correction's size
    std::uint16_t local_port() const { return transport_ ? transport_->local_endpoint().port : 0; }
    void set_impairment(const net::ImpairmentConfig& c) {
        cfg_.impairment = c;
        if (transport_) transport_->set_impairment(c);
    }

private:
    void receive(std::uint64_t now_us);
    void handle(const net::Packet& p, std::uint64_t now_us);
    void on_snapshot(const net::Snapshot& s, std::uint64_t now_us);
    void produce_input(std::uint64_t now_us);
    void send(const net::Message& m, std::uint64_t now_us);
    void set_state(ClientState s, std::string_view why);
    void begin_reconnect(std::uint64_t now_us, std::string_view why);

    ClientConfig cfg_;
    Logger& log_;
    std::unique_ptr<net::Transport> transport_;
    std::mt19937_64 rng_;
    ClientStats stats_;

    ClientState state_ = ClientState::Idle;
    std::optional<net::RejectReason> reject_reason_;
    std::optional<net::DisconnectReason> disconnect_reason_;
    std::uint64_t nonce_ = 0, token_ = 0;
    std::uint32_t session_ = 0, send_seq_ = 0, peer_seq_ = 0, accept_seq_ = 0;
    std::uint16_t player_id_ = 0, entity_id_ = 0, tick_rate_hz_ = 30;
    std::uint64_t phase_started_us_ = 0, last_attempt_us_ = 0, last_recv_us_ = 0, last_heartbeat_us_ = 0;
    std::uint64_t next_input_us_ = 0, last_snapshot_us_ = 0;
    bool attempted_ = false;
    std::optional<std::uint32_t> latest_tick_;
    std::optional<double> rtt_us_;
    std::vector<float> rtt_samples_ms_, correction_samples_;
    std::uint64_t reconnect_started_us_ = 0;

    float move_x_ = 0, move_z_ = 0;
    std::uint32_t input_seq_ = 0;
    std::deque<net::InputCmd> pending_inputs_;
    std::optional<sim::Entity> predicted_, authoritative_;
    std::map<std::uint16_t, sim::InterpolationBuffer> remotes_;
};

}  // namespace mordor::client
