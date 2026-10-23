#include "network/udp_socket.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

#ifndef SIO_UDP_CONNRESET  // documented Winsock IOCTL; not declared by every SDK header set
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace mordor::net {
namespace {

struct WsaInit {
    bool ok;
    WsaInit() {
        WSADATA d;
        ok = WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }
    ~WsaInit() {
        if (ok) WSACleanup();
    }
};

bool ensure_wsa() {
    static WsaInit init;
    return init.ok;
}

sockaddr_in to_sockaddr(const Endpoint& e) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(e.ip);
    a.sin_port = htons(e.port);
    return a;
}

void set_error(std::string* out, const char* what) {
    if (out) *out = std::string(what) + " failed (WSA error " + std::to_string(WSAGetLastError()) + ")";
}

}  // namespace

std::string Endpoint::to_string() const {
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xFF) + "." + std::to_string((ip >> 8) & 0xFF) +
           "." + std::to_string(ip & 0xFF) + ":" + std::to_string(port);
}

std::optional<Endpoint> Endpoint::parse(std::string_view host, std::uint16_t port) {
    if (host == "localhost") return loopback(port);
    if (!ensure_wsa()) return std::nullopt;
    in_addr addr{};
    if (inet_pton(AF_INET, std::string(host).c_str(), &addr) != 1) return std::nullopt;
    return Endpoint{ntohl(addr.s_addr), port};
}

bool UdpSocket::open(const Endpoint& bind_to, std::string* error) {
    close();
    if (!ensure_wsa()) {
        set_error(error, "WSAStartup");
        return false;
    }
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        set_error(error, "socket");
        return false;
    }
    u_long nonblocking = 1;
    // Windows reports ICMP port-unreachable as WSAECONNRESET on later recvfrom calls; disable that behaviour.
    BOOL report_reset = FALSE;
    DWORD bytes = 0;
    sockaddr_in a = to_sockaddr(bind_to);
    if (ioctlsocket(s, FIONBIO, &nonblocking) != 0 ||
        WSAIoctl(s, SIO_UDP_CONNRESET, &report_reset, sizeof report_reset, nullptr, 0, &bytes, nullptr, nullptr) != 0 ||
        bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        set_error(error, "socket setup/bind");
        closesocket(s);
        return false;
    }
    int len = sizeof a;
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    local_ = {ntohl(a.sin_addr.s_addr), ntohs(a.sin_port)};
    handle_ = static_cast<std::uintptr_t>(s);
    return true;
}

void UdpSocket::close() {
    if (handle_ != kInvalid) {
        closesocket(static_cast<SOCKET>(handle_));
        handle_ = kInvalid;
    }
}

bool UdpSocket::send_to(const Endpoint& to, std::span<const std::uint8_t> data) {
    if (!is_open()) return false;
    sockaddr_in a = to_sockaddr(to);
    const int n = sendto(static_cast<SOCKET>(handle_), reinterpret_cast<const char*>(data.data()),
                         static_cast<int>(data.size()), 0, reinterpret_cast<sockaddr*>(&a), sizeof a);
    return n == static_cast<int>(data.size());
}

RecvResult UdpSocket::recv_from(Endpoint& from, std::span<std::uint8_t> buffer) {
    if (!is_open()) return {RecvStatus::Error, 0, 0};
    sockaddr_in a{};
    int len = sizeof a;
    const int n = recvfrom(static_cast<SOCKET>(handle_), reinterpret_cast<char*>(buffer.data()),
                           static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&a), &len);
    if (n == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAECONNRESET) return {RecvStatus::Empty, 0, err};
        if (err == WSAEMSGSIZE) return {RecvStatus::Oversized, buffer.size(), err};
        return {RecvStatus::Error, 0, err};
    }
    from = {ntohl(a.sin_addr.s_addr), ntohs(a.sin_port)};
    return {RecvStatus::Data, static_cast<std::size_t>(n), 0};
}

}  // namespace mordor::net
