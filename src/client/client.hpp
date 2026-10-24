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
#include "network/secure_transport.hpp"
#include "network/transport.hpp"
#include "simulation/world.hpp"

namespace mordor::client {

enum class ClientState { Idle, Connecting, Connected, Reconnecting, Rejected, Disconnected, Failed, Leaving };
const char* to_string(ClientState s);

// Application policy for the overall connect deadline (review #6 answer 3): configurable, explicitly bounded.
inline constexpr std::uint64_t kMinConnectDeadlineUs = 1'000'000;
inline constexpr std::uint64_t kMaxConnectDeadlineUs = 120'000'000;

struct ClientConfig {
    net::Endpoint host = net::Endpoint::loopback(47800);
    std::uint64_t timeout_us = 3'000'000;
    std::uint64_t heartbeat_interval_us = 250'000;
    std::uint64_t connect_retry_us = 250'000;
    std::uint64_t connect_timeout_us = 5'000'000;
    std::uint64_t reconnect_retry_us = 500'000;
    std::uint64_t reconnect_window_us = 15'000'000;  // hard bound per reconnect: never reset by transport retries
    std::uint64_t leave_timeout_us = 1'000'000;      // how long leave() waits for the host's DisconnectAck
    std::uint64_t leave_resend_us = 100'000;
    // Hard upper bound for one connect() call, whatever the transport does: transport establishment (GNS
    // TimeoutInitial 10 s, D-014) + session handshake (connect_timeout_us). Review #5 FB-5-02.
    std::uint64_t connect_deadline_us = 20'000'000;
    double interp_delay_ticks = 3.0;
    bool prediction = true;
    std::uint16_t protocol_version = net::kProtocolVersion;  // overridable only to test version rejection
    net::ImpairmentConfig impairment;
    net::TransportKind transport = net::TransportKind::Udp;
    // When set, ALL traffic goes through the secure transport decorator (DTLS + pinned keys + invitation binding).
    // If this build or config cannot provide it, start() FAILS: there is no plaintext fallback (review #7 answer 3).
    std::shared_ptr<const net::SecureTransportConfig> secure;
};

struct ClientStats {
    std::uint64_t packets_in = 0, packets_out = 0, invalid_packets = 0, foreign_packets = 0, snapshots_applied = 0,
                  stale_snapshots = 0, corrections = 0, reconnects = 0;
    std::uint64_t bytes_in = 0, bytes_out = 0;  // UDP payload bytes (headers included, IP/UDP overhead excluded)
    std::uint64_t stale_notices = 0;  // Disconnect packets older than the accept of the current connection
    std::uint64_t send_errors = 0, transport_disconnects = 0;  // non-Sent SendResults; backend-detected host loss
    std::uint64_t last_reconnect_ms = 0, max_reconnect_ms = 0;  // timeout detection -> accepted again
    float max_correction = 0;
    std::uint64_t leave_disconnects_sent = 0, leave_disconnects_refused = 0;  // explicit leave (FB-10-04)
    bool leave_confirmed = false;
    std::uint64_t leave_ms = 0;
    // Application-level reliable events (FB-9-02, KI-28).
    std::uint64_t events_received = 0, events_duplicate = 0, events_out_of_order = 0, events_discarded_new_session = 0;
};

// A consequential event from the host, delivered in order exactly once (FB-9-02).
struct ReceivedEvent {
    std::uint32_t id = 0;
    std::uint16_t kind = 0;
    std::vector<std::uint8_t> payload;
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
    const ClientConfig& config() const { return cfg_; }
    const net::TransportStats* transport_stats() const { return transport_ ? &transport_->stats() : nullptr; }
    const net::SecureStats* transport_stats_secure() const { return transport_ ? net::secure_stats(*transport_) : nullptr; }
    // Events received since the last call, in order, each exactly once (FB-9-02). The host keeps resending them
    // until the application reports them applied with ack_events_applied(); call it only after applying.
    std::vector<ReceivedEvent> take_events();
    // Explicit leave (review #10 FB-10-04): sends Disconnect and keeps resending it until the host's DisconnectAck
    // arrives or leave_timeout_us passes. Keep calling update() while leaving(); afterwards stats().leave_confirmed
    // tells a confirmed clean leave from an unconfirmed one. disconnect() remains the fire-and-forget variant.
    void leave(std::uint64_t now_us);
    bool leaving() const { return state_ == ClientState::Leaving; }
    void ack_events_applied(std::uint32_t through_id, std::uint64_t now_us);
    std::uint32_t events_applied_through() const { return applied_through_; }
    std::uint16_t entity_id() const { return entity_id_; }
    std::uint32_t session_id() const { return session_; }
    std::optional<std::uint32_t> latest_server_tick() const { return latest_tick_; }
    std::optional<double> rtt_ms() const;
    std::optional<net::Transform> predicted_self() const;
    std::optional<net::Transform> authoritative_self() const;
    std::optional<net::Transform> remote_view(std::uint16_t entity_id, std::uint64_t now_us) const;
    std::vector<std::uint16_t> remote_entities() const;
    const ClientStats& stats() const { return stats_; }
    const std::string& state_reason() const { return state_reason_; }  // why the last state change happened
    const std::vector<float>& rtt_samples_ms() const { return rtt_samples_ms_; }  // raw samples for percentiles
    const std::vector<float>& correction_samples() const { return correction_samples_; }  // each correction's size
    std::uint16_t local_port() const { return transport_ ? transport_->local_endpoint().port : 0; }
    bool set_impairment(const net::ImpairmentConfig& c) {  // false: refused by the transport (FB-5-03)
        if (transport_ && !transport_->set_impairment(c)) return false;
        cfg_.impairment = c;
        return true;
    }

private:
    void receive(std::uint64_t now_us);
    void handle(const net::Packet& p, std::uint64_t now_us);
    void on_snapshot(const net::Snapshot& s, std::uint64_t now_us);
    void produce_input(std::uint64_t now_us);
    bool send(const net::Message& m, std::uint64_t now_us, net::Delivery delivery = net::Delivery::Unreliable);
    void set_state(ClientState s, std::string_view why);
    void begin_reconnect(std::uint64_t now_us, std::string_view why);
    net::Delivery ack_delivery() const;

    ClientConfig cfg_;
    Logger& log_;
    std::unique_ptr<net::Transport> transport_;
    std::optional<net::PeerId> server_peer_;  // transport identity of the host connection (FB-4-02)
    std::string state_reason_;
    std::uint64_t connect_started_us_ = 0;     // connect() call time: connect_deadline_us counts from here
    bool transport_up_ = false;                // transport reported Connected for server_peer_ (D-014)
    std::vector<net::TransportEvent> events_;
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
    std::uint32_t event_session_ = 0, next_event_ = 1, applied_through_ = 0;  // per host session
    std::uint64_t leave_started_us_ = 0, last_leave_send_us_ = 0;
    bool awaiting_first_snapshot_ = false;
    std::vector<ReceivedEvent> event_inbox_;

    float move_x_ = 0, move_z_ = 0;
    std::uint32_t input_seq_ = 0;
    std::deque<net::InputCmd> pending_inputs_;
    std::optional<sim::Entity> predicted_, authoritative_;
    std::map<std::uint16_t, sim::InterpolationBuffer> remotes_;
};

}  // namespace mordor::client
