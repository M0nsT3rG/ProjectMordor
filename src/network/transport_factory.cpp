#include "network/transport.hpp"

#include "network/udp_transport.hpp"
#ifdef MORDOR_HAVE_GNS
#include "network/gns_transport.hpp"
#endif

namespace mordor::net {

const char* to_string(TransportKind k) { return k == TransportKind::Gns ? "gns" : "udp"; }

const char* to_string(SendResult r) {
    switch (r) {
        case SendResult::Sent: return "sent";
        case SendResult::Unsupported: return "unsupported";
        case SendResult::NotConnected: return "not_connected";
        case SendResult::TooLarge: return "too_large";
        case SendResult::Backpressure: return "backpressure";
        case SendResult::Closed: return "closed";
        case SendResult::Failed: return "failed";
    }
    return "?";
}

std::optional<TransportKind> parse_transport(std::string_view s) {
    if (s == "udp") return TransportKind::Udp;
    if (s == "gns") return TransportKind::Gns;
    return std::nullopt;
}

bool transport_available(TransportKind k) {
#ifdef MORDOR_HAVE_GNS
    (void)k;
    return true;
#else
    return k == TransportKind::Udp;
#endif
}

std::unique_ptr<Transport> make_transport(TransportKind kind, const ImpairmentConfig& impairment, std::string* error) {
    if (kind == TransportKind::Udp) return std::make_unique<UdpTransport>(impairment);
#ifdef MORDOR_HAVE_GNS
    (void)error;
    return std::make_unique<GnsTransport>(impairment);
#else
    // The GNS adapter is the next milestone (review #3 FB-3-02); until then only the UDP backend exists.
    if (error) *error = "the GameNetworkingSockets transport adapter is not available in this build";
    return nullptr;
#endif
}

}  // namespace mordor::net
