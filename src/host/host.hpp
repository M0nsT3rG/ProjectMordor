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
#include "network/udp_socket.hpp"
#include "simulation/world.hpp"

namespace mordor::host {

struct HostConfig {
    net::Endpoint bind = net::Endpoint::loopback(0);  // loopback by default; LAN needs explicit opt-in
    std::uint16_t tick_rate_hz = 30;
    std::uint8_t max_players = 2;
    std::uint64_t timeout_us = 3'000'000;
    std::uint64_t reconnect_grace_us = 30'000'000;
    net::ImpairmentConfig impairment;
};

struct HostStats {
    std::uint64_t packets_in = 0, packets_out = 0, invalid_packets = 0, rejected_version = 0, rejected_full = 0,
                  unknown_peer = 0, stale_inputs = 0, joins = 0, reconnects = 0, timeouts = 0;
};

class Host {
public:
    Host(HostConfig cfg, Logger& log);
    bool start(std::string* error = nullptr);
    void update(std::uint64_t now_us);
    void shutdown();  // notifies connected clients and releases the socket
    bool running() const { return running_; }

    std::uint16_t port() const { return socket_.local_endpoint().port; }
    std::uint32_t session_id() const { return session_; }
    std::size_t connected_count() const;
    std::size_t slot_count() const { return slots_.size(); }
    const sim::World& world() const { return world_; }
    const HostStats& stats() const { return stats_; }
    std::optional<net::Transform> player_transform(std::uint16_t player_id) const;
    void set_impairment(const net::ImpairmentConfig& c) { sender_.set_config(c); }

private:
    struct Slot {
        std::uint16_t player_id = 0;
        std::uint16_t entity_id = 0;
        net::Endpoint endpoint;
        bool connected = false;
        std::uint64_t token = 0;
        std::uint64_t nonce = 0;
        std::uint64_t last_recv_us = 0;
        std::uint64_t disconnected_at_us = 0;
        std::uint32_t last_received_input = 0;
        std::uint32_t last_processed_input = 0;
        std::uint32_t send_seq = 0;
        std::uint32_t peer_seq = 0;
        std::deque<net::InputCmd> pending;
    };

    void receive(std::uint64_t now_us);
    void handle(const net::Endpoint& from, const net::Packet& p, std::uint64_t now_us);
    void on_connect(const net::Endpoint& from, const net::ConnectRequest& req, std::uint64_t now_us);
    void step();
    void broadcast(std::uint64_t now_us);
    void check_timeouts(std::uint64_t now_us);
    void send(Slot& s, const net::Message& m, std::uint64_t now_us);
    void send_unconnected(const net::Endpoint& to, const net::Message& m, std::uint64_t now_us);
    void accept(Slot& s, std::uint64_t now_us);
    void remove_slot(std::uint16_t player_id);
    Slot* find_by_endpoint(const net::Endpoint& e);
    std::uint64_t random_nonzero();

    HostConfig cfg_;
    Logger& log_;
    net::UdpSocket socket_;
    net::ImpairedSender sender_;
    std::mt19937_64 rng_;
    sim::World world_;
    std::vector<Slot> slots_;
    HostStats stats_;
    std::uint32_t session_ = 0;
    std::uint16_t next_player_id_ = 1;
    bool running_ = false;
    bool clock_started_ = false;
    std::uint64_t next_tick_us_ = 0;
};

}  // namespace mordor::host
