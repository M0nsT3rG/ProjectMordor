#pragma once
// Raw Winsock2 UDP backend behind Transport contract v2. Connectionless: a PeerId is created on the first
// datagram from (or connect() to) an address; close_peer() forgets it, and later traffic from that address
// becomes a new PeerId with generation + 1. ReliableOrdered is not provided (SendResult::Unsupported).
#include <map>

#include "network/transport.hpp"

namespace mordor::net {

class UdpTransport final : public Transport {
public:
    explicit UdpTransport(const ImpairmentConfig& impairment);
    bool open(const Endpoint& bind, bool listen, std::string* error) override;
    void close() override;
    bool is_open() const override { return socket_.is_open(); }
    Endpoint local_endpoint() const override { return socket_.local_endpoint(); }
    TransportCapabilities capabilities() const override;
    std::optional<PeerId> connect(const Endpoint& remote) override;
    SendResult send(PeerId peer, std::span<const std::uint8_t> bytes, Delivery d, std::uint64_t now_us) override;
    void poll(std::uint64_t now_us, std::vector<TransportEvent>& out) override;
    void close_peer(PeerId peer) override;
    void flush(std::uint64_t now_us) override { sender_.flush(now_us); }
    void flush_all() override { sender_.flush_all(); }
    bool set_impairment(const ImpairmentConfig& c) override {
        sender_.set_config(c);
        return true;
    }
    TransportKind kind() const override { return TransportKind::Udp; }
    const TransportStats& stats() const override;

private:
    PeerId peer_for(const Endpoint& e);  // existing or new (queues a Connected event)

    UdpSocket socket_;
    ImpairedSender sender_;
    std::map<Endpoint, PeerId> by_endpoint_;
    std::map<std::uint32_t, Endpoint> by_id_;
    std::map<Endpoint, std::uint32_t> generations_;
    std::vector<TransportEvent> pending_;  // events produced outside poll()
    std::uint32_t next_id_ = 1;
    mutable TransportStats stats_;
};

}  // namespace mordor::net
