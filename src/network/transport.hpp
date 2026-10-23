#pragma once
// Datagram-style transport seam (D-012, docs/transport-interface.md). Host and client logic speak our
// protocol through this interface and never touch sockets, so the same tests and soak gates run on every
// backend. Peers are identified by their remote Endpoint on all backends; connection-oriented backends
// (GameNetworkingSockets) keep their own endpoint <-> connection mapping.
// Impairment is applied ABOVE the backend at message level by ImpairedSender, so every backend sees
// identical loss/latency/jitter/duplication for the messages we send.
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
bool transport_available(TransportKind k);  // Gns only when built with MORDOR_WITH_GNS

class Transport {
public:
    virtual ~Transport() = default;
    // listen = true for the host. A client backend may ignore `bind` (connection-oriented backends).
    virtual bool open(const Endpoint& bind, bool listen, std::string* error) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    virtual Endpoint local_endpoint() const = 0;
    // Unreliable, unordered datagram. A client backend opens a connection to `to` on first use if needed.
    virtual void send(const Endpoint& to, std::vector<std::uint8_t> bytes, std::uint64_t now_us) = 0;
    // Non-blocking receive with UdpSocket semantics (Empty when nothing is pending).
    virtual RecvResult recv(Endpoint& from, std::span<std::uint8_t> buffer) = 0;
    virtual void flush(std::uint64_t now_us) = 0;  // release due impaired packets, service the backend
    virtual void flush_all() = 0;                  // shutdown path: send everything now
    virtual void set_impairment(const ImpairmentConfig& c) = 0;
    virtual TransportKind kind() const = 0;
};

std::unique_ptr<Transport> make_transport(TransportKind kind, const ImpairmentConfig& impairment, std::string* error);

}  // namespace mordor::net
