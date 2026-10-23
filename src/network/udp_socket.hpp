#pragma once
// Thin non-blocking IPv4 UDP socket over documented Winsock2 APIs.
// shortcut: IPv4 only; add IPv6 when testing over networks that require it.
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mordor::net {

struct Endpoint {
    std::uint32_t ip = 0;  // host byte order
    std::uint16_t port = 0;
    auto operator<=>(const Endpoint&) const = default;
    std::string to_string() const;
    bool is_loopback() const { return (ip >> 24) == 127; }
    static std::optional<Endpoint> parse(std::string_view host, std::uint16_t port);
    static Endpoint loopback(std::uint16_t port) { return {0x7F000001u, port}; }
};

enum class RecvStatus { Data, Empty, Oversized, Error };
struct RecvResult {
    RecvStatus status = RecvStatus::Empty;
    std::size_t size = 0;
    int os_error = 0;
};

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket() { close(); }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool open(const Endpoint& bind_to, std::string* error = nullptr);
    void close();
    bool is_open() const { return handle_ != kInvalid; }
    Endpoint local_endpoint() const { return local_; }
    bool send_to(const Endpoint& to, std::span<const std::uint8_t> data);
    RecvResult recv_from(Endpoint& from, std::span<std::uint8_t> buffer);

private:
    static constexpr std::uintptr_t kInvalid = ~std::uintptr_t{0};
    std::uintptr_t handle_ = kInvalid;
    Endpoint local_{};
};

}  // namespace mordor::net
