#pragma once
// Transport contract v2 (review #4 FB-4-01/02/04; docs/transport-interface.md). Host and client logic speak our
// protocol through this interface and never touch sockets, so the same tests run on every backend.
//
// Identity: every remote peer is a PeerId {id, generation}. Ids are never reused within one Transport; a peer
// that goes away and comes back (same address or not) gets a NEW PeerId. Sends to a stale PeerId fail.
// Lifecycle: poll() reports Connected before the first Message of a peer, and Disconnected when the backend
// knows the peer is gone. Connectionless backends (raw UDP) report Connected on the first datagram from a new
// address and Disconnected only for close_peer()/close(); liveness is then the application's job.
// Delivery: Unreliable (may drop, duplicate, reorder) or ReliableOrdered. A backend that cannot provide a class
// returns SendResult::Unsupported - it never silently downgrades.
// Errors: send() reports local acceptance. Loss inside the (real or simulated) network is NOT an error, exactly
// as on a real network. Anything accepted locally but never transmitted is counted in TransportStats.
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "network/impairment.hpp"
#include "network/udp_socket.hpp"

namespace mordor::net {

enum class TransportKind { Udp, Gns };
const char* to_string(TransportKind k);
std::optional<TransportKind> parse_transport(std::string_view s);
bool transport_available(TransportKind k);  // Gns only when the GNS adapter is built

struct PeerId {
    std::uint32_t id = 0;          // 0 = invalid; never reused within one Transport instance
    std::uint32_t generation = 0;  // how many earlier peers this transport saw at the same remote address
    auto operator<=>(const PeerId&) const = default;
    bool valid() const { return id != 0; }
};

enum class Delivery { Unreliable, ReliableOrdered };

enum class SendResult {
    Sent,          // handed to the backend (or to the simulated network); may still be lost in the network
    Unsupported,   // backend cannot provide this delivery class
    NotConnected,  // unknown or stale PeerId
    TooLarge,      // payload above max_payload()
    Backpressure,  // backend send queue full; try later
    Closed,        // transport not open
    Failed,        // local backend error (e.g. OS send error)
};
const char* to_string(SendResult r);

struct TransportEvent {
    enum class Kind { Connected, Disconnected, Message, DroppedInbound };
    Kind kind = Kind::Message;
    PeerId peer;                       // invalid for DroppedInbound from an unknown sender
    Endpoint remote;                   // informational only; never use it as identity
    Delivery delivery = Delivery::Unreliable;
    std::vector<std::uint8_t> bytes;   // Message only
    std::string_view reason;           // Disconnected / DroppedInbound: static string
};

struct TransportStats {
    std::uint64_t sent = 0, send_failed = 0, unsupported = 0, too_large = 0, not_connected = 0, backpressure = 0;
    std::uint64_t received = 0, dropped_inbound_oversized = 0;
    std::uint64_t accepted_then_failed = 0;     // accepted by send() but the delayed transmit failed
    std::uint64_t discarded_on_close = 0;       // accepted by send() but still queued when close() ran
    std::uint64_t simulated_drops = 0;          // removed by impairment simulation (network behaviour, not an error)
};

struct TransportCapabilities {
    bool reliable_ordered = false;
    bool reports_disconnects = false;  // backend detects peer loss itself
    std::size_t max_payload = 0;
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool open(const Endpoint& bind, bool listen, std::string* error) = 0;
    virtual void close() = 0;  // queued-but-unsent data is counted in discarded_on_close
    virtual bool is_open() const = 0;
    virtual Endpoint local_endpoint() const = 0;
    virtual TransportCapabilities capabilities() const = 0;
    // Client side: start talking to `remote`; returns the PeerId to use. Idempotent while that peer is live.
    virtual std::optional<PeerId> connect(const Endpoint& remote) = 0;
    virtual SendResult send(PeerId peer, std::span<const std::uint8_t> bytes, Delivery d, std::uint64_t now_us) = 0;
    virtual void poll(std::uint64_t now_us, std::vector<TransportEvent>& out) = 0;  // receive + service + flush due
    virtual void flush(std::uint64_t now_us) = 0;  // send whatever is due, without receiving
    virtual void close_peer(PeerId peer) = 0;  // forget this peer; later traffic from its address is a new PeerId
    virtual void flush_all() = 0;              // shutdown path: send everything queued now
    virtual void set_impairment(const ImpairmentConfig& c) = 0;
    virtual TransportKind kind() const = 0;
    virtual const TransportStats& stats() const = 0;
};

std::unique_ptr<Transport> make_transport(TransportKind kind, const ImpairmentConfig& impairment, std::string* error);

}  // namespace mordor::net
