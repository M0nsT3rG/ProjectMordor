#pragma once
// Secure transport decorator (review #7 answer 3 / FB-7-05): implements Transport on top of any backend and gates
// ALL traffic through DtlsSession (DTLS 1.2 + RFC 7250 pinned keys + fail-closed invitation binding).
//  - Connected is reported only after DTLS and the binding succeeded; Messages only from authenticated peers.
//  - No plaintext fallback: a datagram that is not part of a DTLS session is dropped and counted.
//  - Delivery::ReliableOrdered is our own channel INSIDE the authenticated DTLS session (review #8 FB-8-02):
//    sequence numbers, cumulative acks, retransmission, in-order delivery, dedup, a 256-message send window
//    (Backpressure beyond). The inner backend's reliable class is never used, so nothing bypasses DTLS. Accepted
//    reliable messages lost with their session are counted (accepted_then_failed), never silent.
//  - reports_disconnects == true means: DTLS rejection, inner-transport loss, replacement by a new session, and
//    idle_timeout_us of silence are reported. Liveness finer than that remains the session layer's job (FB-8-04).
//  - Pre-authentication state is BOUNDED (FB-7-01): one pending session per inner peer, max_pending_total
//    overall (a peer with an established session has its own reserved reconnect slot, FB-8-01), each living at
//    most pending_lifetime_us. A new handshake attempt displaces a pending one only if that one has no verified
//    cookie, or if it carries a valid cookie itself (FB-8-01). The HelloVerifyRequest cookie is STATEFUL (a session is
//    allocated before it): an integrity measure, not a DoS gate. Lab / loopback only (KI-29).
//  - A ClientHello on an established association (UDP reconnect from the same address) opens a pending session
//    next to it; only once that one authenticates is the old one retired (Disconnected "replaced") and the new
//    one reported Connected under a NEW PeerId. Records of the retired session are dropped by the new one.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "network/protocol.hpp"
#include "network/transport.hpp"

typedef struct evp_pkey_st EVP_PKEY;

namespace mordor::net {

struct Invitation {
    std::string id;                    // given out of band with the host's key fingerprint
    std::uint16_t protocol_version = kProtocolVersion;
    std::uint64_t expires_unix_s = 0;  // wall clock; the invitation is refused after this
    auto operator<=>(const Invitation&) const = default;
};

struct SecureTransportConfig {
    EVP_PKEY* own_key = nullptr;                             // not owned; must outlive the transport
    std::vector<std::vector<std::uint8_t>> pinned_peers;     // DER SPKI of every peer allowed to connect
    Invitation invitation;
    std::size_t max_pending_total = 4;
    std::uint64_t pending_lifetime_us = 10'000'000;          // handshake + binding must finish within this
    std::uint64_t binding_timeout_us = 5'000'000;
    // Review #8 FB-8-04: an authenticated peer silent for this long is reported Disconnected("idle_timeout"), so
    // reports_disconnects holds even over raw UDP. Longer than the session layer's 3 s liveness, which normally
    // acts first; this bounds how long a vanished peer's DTLS state can linger. 0 disables.
    std::uint64_t idle_timeout_us = 10'000'000;
    std::function<std::uint64_t()> unix_clock;               // wall-clock seconds; default std::time
};

struct SecureStats {
    std::uint64_t pending_opened = 0, pending_refused_cap = 0, pending_failed = 0;
    std::uint64_t pending_replaced = 0;   // an unverified (or cookie-bearing) new handshake displaced a pending one
    std::uint64_t hello_ignored = 0;      // new ClientHello without a valid cookie vs a cookie-verified pending
    std::uint64_t authenticated = 0, promotions_replacing = 0, rejected_established = 0, idle_timeouts = 0;
    std::uint64_t dropped_not_dtls = 0;      // plaintext / non-ClientHello from a peer without a session
    std::uint64_t dropped_unknown_peer = 0;  // client side: datagram from a peer we never connected to
    std::uint64_t reliable_retransmits = 0, reliable_duplicates = 0;
};

// Wraps `inner` (already constructed, not yet opened). Returns nullptr with *error when this build has no DTLS
// (MORDOR_WITH_GNS=OFF) or the config is unusable: callers must then FAIL, never fall back to plaintext.
std::unique_ptr<Transport> make_secure_transport(std::unique_ptr<Transport> inner, const SecureTransportConfig& cfg,
                                                 std::string* error);

// The decorator's own counters, or nullptr if `t` is not a secure transport.
const SecureStats* secure_stats(const Transport& t);

}  // namespace mordor::net
