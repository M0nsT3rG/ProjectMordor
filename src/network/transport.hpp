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
    Sent,          // accepted by the backend (or the simulated network). NOT delivery: it may still be lost, and
                   // even delivered data may never be processed by the peer application (FB-5-01, FB-6-04)
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
    // All counters are WIRE-LEVEL DIAGNOSTICS, not proof of delivery: even an acknowledged reliable message may
    // never be processed by the peer application. Consequential events need application-level ids and acks on
    // top (review #5 FB-5-01).
    std::uint64_t accepted_then_failed = 0;     // accepted, then never wire-acknowledged (failed transmit, or the
                                                // connection died with reliable data still unacknowledged).
                                                // Per message where exact_loss_accounting, else per connection.
    std::uint64_t discarded_on_close = 0;       // MESSAGES accepted but still queued when close() ran
                                                // (exact_loss_accounting backends only; others leave it 0)
    std::uint64_t closed_with_unacked_reliable = 0;  // CONNECTIONS closed (close_peer() or close()) while reliable
                                                     // data was still unacknowledged
    std::uint64_t impairment_refused = 0;       // set_impairment() refused: process-global impairment held by another
    std::uint64_t simulated_drops = 0;          // removed by impairment simulation (network behaviour, not an error)
};

struct TransportCapabilities {
    bool reliable_ordered = false;
    bool reports_disconnects = false;  // backend detects peer loss itself (connection-oriented)
    std::size_t max_payload = 0;
    // true: simulated_drops and discarded_on_close are exact per-message counts (UDP: impairment runs in our code).
    // false: the backend's fault injection and queues are internal (GNS); counts are per connection and cover
    // reliable data only. Loss of reliable data is still never silent: see accepted_then_failed /
    // closed_with_unacked_reliable / discarded_on_close.
    bool exact_loss_accounting = false;
    // false: impairment is process-global (GNS fake-packet settings), so set_impairment() affects every instance;
    // a different config while another instance holds one is refused (set_impairment returns false).
    bool per_instance_impairment = false;
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool open(const Endpoint& bind, bool listen, std::string* error) = 0;
    virtual void close() = 0;  // queued-but-unsent data is counted (discarded_on_close / closed_with_unacked_reliable)
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
    virtual bool set_impairment(const ImpairmentConfig& c) = 0;  // false: refused, nothing changed
    virtual TransportKind kind() const = 0;
    virtual const TransportStats& stats() const = 0;
};

std::unique_ptr<Transport> make_transport(TransportKind kind, const ImpairmentConfig& impairment, std::string* error);

}  // namespace mordor::net
