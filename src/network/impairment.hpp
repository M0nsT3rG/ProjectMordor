#pragma once
// Deterministic outgoing-traffic impairment (loss, latency, jitter, duplication) for tests and demos.
// Jitter naturally reorders packets because each copy gets its own delivery time.
#include <cstdint>
#include <functional>
#include <random>
#include <span>
#include <vector>

#include "network/udp_socket.hpp"

namespace mordor::net {

struct ImpairmentConfig {
    double loss = 0.0;       // probability [0,1] that a packet is dropped
    double duplicate = 0.0;  // probability [0,1] that a packet is sent twice
    std::uint32_t latency_ms = 0;
    std::uint32_t jitter_ms = 0;  // extra uniform delay in [0, jitter_ms]
    std::uint64_t seed = 1;
    bool enabled() const { return loss > 0 || duplicate > 0 || latency_ms > 0 || jitter_ms > 0; }
    bool valid() const { return loss >= 0 && loss <= 1 && duplicate >= 0 && duplicate <= 1; }
};

struct ImpairmentStats {
    std::uint64_t offered = 0, sent = 0, dropped = 0, duplicated = 0, send_failures = 0;
    std::uint64_t delayed_send_failures = 0;  // subset of send_failures: accepted, queued, then failed on transmit
};

// What happened to one send() call, so callers can surface local failures (review FB-4-04).
enum class ImpairOutcome { Sent, Failed, Queued, SimulatedDrop };

class ImpairedSender {
public:
    using Transmit = std::function<bool(const Endpoint&, std::span<const std::uint8_t>)>;
    ImpairedSender(UdpSocket& socket, ImpairmentConfig cfg)
        : ImpairedSender([&socket](const Endpoint& to, std::span<const std::uint8_t> b) { return socket.send_to(to, b); }, cfg) {}
    // Any backend can sit underneath; impairment stays identical (D-012).
    ImpairedSender(Transmit transmit, ImpairmentConfig cfg) : transmit_fn_(std::move(transmit)), cfg_(cfg), rng_(cfg.seed) {}
    void set_config(const ImpairmentConfig& cfg) { cfg_ = cfg; }
    const ImpairmentConfig& config() const { return cfg_; }
    ImpairOutcome send(const Endpoint& to, std::vector<std::uint8_t> bytes, std::uint64_t now_us);
    void flush(std::uint64_t now_us);  // send everything that is due
    void flush_all();                  // send everything now (shutdown path)
    void clear() { queue_.clear(); }
    std::size_t queued() const { return queue_.size(); }
    const ImpairmentStats& stats() const { return stats_; }

private:
    struct Pending {
        std::uint64_t due_us;
        Endpoint to;
        std::vector<std::uint8_t> bytes;
    };
    bool transmit(const Endpoint& to, const std::vector<std::uint8_t>& bytes, bool delayed);

    Transmit transmit_fn_;
    ImpairmentConfig cfg_;
    std::mt19937_64 rng_;
    std::vector<Pending> queue_;
    ImpairmentStats stats_;
};

}  // namespace mordor::net
