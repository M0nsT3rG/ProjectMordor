#pragma once
// Host-authoritative session: owns the simulated world, applies client inputs at a fixed tick
// and broadcasts snapshots. Time is injected (now_us) so tests can drive it deterministically.
#include <cstdint>
#include <deque>
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

namespace mordor::host {

struct HostConfig {
    net::Endpoint bind = net::Endpoint::loopback(0);  // loopback by default; LAN needs explicit opt-in
    std::uint16_t tick_rate_hz = 30;
    std::uint8_t max_players = 2;
    std::uint64_t timeout_us = 3'000'000;
    std::uint64_t reconnect_grace_us = 30'000'000;
    net::ImpairmentConfig impairment;
    net::TransportKind transport = net::TransportKind::Udp;
    // When set, ALL traffic goes through the secure transport decorator (DTLS + pinned keys + invitation binding).
    // If this build or config cannot provide it, start() FAILS: there is no plaintext fallback (review #7 answer 3).
    std::shared_ptr<const net::SecureTransportConfig> secure;
    std::size_t max_unacked_events = 64;           // per player; send_event() returns false beyond (backpressure)
    std::uint64_t event_resend_us = 250'000;       // resend unapplied events this often (reliable transports: x4, >= 1 s)
};

struct HostStats {
    std::uint64_t packets_in = 0, packets_out = 0, invalid_packets = 0, rejected_version = 0, rejected_full = 0,
                  unknown_peer = 0, stale_inputs = 0, shed_inputs = 0, joins = 0, reconnects = 0, timeouts = 0;
    std::uint64_t bytes_in = 0, bytes_out = 0, skipped_ticks = 0, timeout_notices = 0;
    std::uint64_t send_errors = 0, transport_disconnects = 0;  // non-Sent SendResults; backend-detected peer loss
    // Application-level reliable events (FB-9-02, KI-28).
    std::uint64_t events_queued = 0, events_transmitted = 0, events_retransmitted = 0, events_replayed = 0,
                  events_applied = 0, events_backpressure = 0, events_dropped_with_slot = 0,
                  events_send_refused = 0, event_acks_invalid = 0;
    std::uint64_t clean_leaves = 0, leave_acks_repeated = 0;  // explicit leave handshake (FB-10-04)
    std::uint64_t leave_lingers_cut = 0;  // linger ended early because kMaxLingering were held (FB-11-03)
};

class Host {
public:
    Host(HostConfig cfg, Logger& log);
    bool start(std::string* error = nullptr);
    void update(std::uint64_t now_us);
    void shutdown();  // notifies connected clients and releases the socket
    bool running() const { return running_; }

    std::uint16_t port() const { return transport_ ? transport_->local_endpoint().port : 0; }
    std::uint32_t session_id() const { return session_; }
    std::size_t connected_count() const;
    std::size_t slot_count() const { return slots_.size(); }
    const sim::World& world() const { return world_; }
    const HostStats& stats() const { return stats_; }
    std::optional<net::Transform> player_transform(std::uint16_t player_id) const;
    const net::Transport* transport_for_test() const { return transport_.get(); }
    // Queues a consequential event for a player (FB-9-02). It is delivered in order exactly once and kept until that
    // player's APPLICATION acknowledges it, across packet loss and reconnects within the grace period. False: no
    // such player, payload too large, or max_unacked_events reached (backpressure). Not durable across host restart.
    bool send_event(std::uint16_t player_id, std::uint16_t kind, std::span<const std::uint8_t> payload, std::uint64_t now_us);
    std::size_t unapplied_events(std::uint16_t player_id) const;
    std::vector<std::uint16_t> player_ids() const;  // every slot, including players in their grace period
    bool set_impairment(const net::ImpairmentConfig& c) {  // false: refused by the transport (FB-5-03)
        if (transport_ && !transport_->set_impairment(c)) return false;
        cfg_.impairment = c;
        return true;
    }

private:
    struct Slot {
        std::uint16_t player_id = 0;
        std::uint16_t entity_id = 0;
        net::PeerId peer;  // transport identity of the current connection (FB-4-02)
        bool connected = false;
        std::uint64_t token = 0;
        std::uint64_t nonce = 0;
        std::uint64_t last_recv_us = 0;
        std::uint64_t packets_in = 0;  // session-valid packets from this player (gate v6 per-client delivery ratio)
        std::uint64_t disconnected_at_us = 0;
        std::uint64_t last_notice_us = 0;  // rate limit for "you timed out" notices
        std::uint32_t last_received_input = 0;
        std::uint32_t last_processed_input = 0;
        std::uint32_t send_seq = 0;
        std::uint32_t peer_seq = 0;
        std::deque<net::InputCmd> pending;
        struct OutEvent {
            net::GameEvent event;
            std::uint64_t last_sent_us = 0;  // 0: not sent on the current connection (new, or replay after reconnect)
            std::uint64_t retry_at_us = 0;   // after a refused send (FB-10-02)
            unsigned refusals = 0;
        };
        std::deque<OutEvent> events;  // unapplied, oldest first
        std::uint32_t next_event_id = 1;
        std::uint32_t highest_sent_event = 0;  // highest id the transport accepted in this session (FB-10-01)
    };

    void receive(std::uint64_t now_us);
    void on_datagram(net::PeerId from, std::span<const std::uint8_t> bytes, std::uint64_t now_us);
    void on_transport_disconnect(net::PeerId peer, std::uint64_t now_us);
    void handle(net::PeerId from, const net::Packet& p, std::uint64_t now_us);
    void on_connect(net::PeerId from, const net::ConnectRequest& req, std::uint64_t now_us);
    void step();
    void broadcast(std::uint64_t now_us);
    void check_timeouts(std::uint64_t now_us);
    void service_events(std::uint64_t now_us);
    net::SendResult send(Slot& s, const net::Message& m, std::uint64_t now_us, net::Delivery delivery = net::Delivery::Unreliable);
    void send_unconnected(net::PeerId to, const net::Message& m, std::uint64_t now_us);
    void ack_leave(net::PeerId to, std::uint64_t now_us);
    net::SendResult transmit(net::PeerId to, const std::vector<std::uint8_t>& bytes, std::uint64_t now_us, net::Delivery delivery);
    void accept(Slot& s, std::uint64_t now_us);
    // close_transport_peer = false when the same transport peer is about to be reused (e.g. new client instance).
    void remove_slot(std::uint16_t player_id, bool close_transport_peer = true);
    Slot* find_by_peer(net::PeerId p);
    void notify_timed_out(net::PeerId from, std::uint64_t now_us);
    std::uint64_t random_nonzero();

    HostConfig cfg_;
    Logger& log_;
    std::unique_ptr<net::Transport> transport_;
    std::vector<net::TransportEvent> events_;
    std::mt19937_64 rng_;
    sim::World world_;
    std::vector<Slot> slots_;
    // Transport peers of players that left cleanly, closed after the client's leave window (KI-37).
    static constexpr std::uint64_t kLeaveLingerUs = 1'500'000;
    static constexpr std::size_t kMaxLingering = 16;
    struct Closing {
        net::PeerId peer;
        std::uint64_t close_at_us, last_ack_us;
    };
    std::vector<Closing> closing_;
    HostStats stats_;
    std::uint32_t session_ = 0;
    std::uint16_t next_player_id_ = 1;
    bool running_ = false;
    bool clock_started_ = false;
    std::uint64_t next_tick_us_ = 0;
    std::uint64_t first_update_us_ = 0, last_update_us_ = 0;
};

}  // namespace mordor::host
