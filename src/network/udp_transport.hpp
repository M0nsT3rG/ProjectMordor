#pragma once
// Raw Winsock2 UDP backend: the original prototype path behind the Transport interface.
#include "network/transport.hpp"

namespace mordor::net {

class UdpTransport final : public Transport {
public:
    explicit UdpTransport(const ImpairmentConfig& impairment) : sender_(socket_, impairment) {}
    bool open(const Endpoint& bind, bool, std::string* error) override { return socket_.open(bind, error); }
    void close() override { socket_.close(); }
    bool is_open() const override { return socket_.is_open(); }
    Endpoint local_endpoint() const override { return socket_.local_endpoint(); }
    void send(const Endpoint& to, std::vector<std::uint8_t> bytes, std::uint64_t now_us) override {
        sender_.send(to, std::move(bytes), now_us);
    }
    RecvResult recv(Endpoint& from, std::span<std::uint8_t> buffer) override { return socket_.recv_from(from, buffer); }
    void flush(std::uint64_t now_us) override { sender_.flush(now_us); }
    void flush_all() override { sender_.flush_all(); }
    void set_impairment(const ImpairmentConfig& c) override { sender_.set_config(c); }
    TransportKind kind() const override { return TransportKind::Udp; }

private:
    UdpSocket socket_;
    ImpairedSender sender_;
};

}  // namespace mordor::net
