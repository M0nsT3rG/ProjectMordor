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
//  - STATELESS cookie gate (KI-30, review #10 answer 5): a single listener answers every new ClientHello with a
//    HelloVerifyRequest via DTLSv1_listen and keeps no per-peer state; only a ClientHello with a valid cookie (the
//    sender receives at its address) creates a pending session. Pre-authentication state is then BOUNDED
//    (FB-7-01): one pending session per inner peer, max_pending_total overall (a peer with an established session
//    reconnects in a reserved slot, FB-8-01), each living at most pending_lifetime_us; a newer cookie-proven
//    attempt replaces an older pending one. New ClientHellos are rate-limited before any work. Lab only (KI-29).
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
    // Review #9 answer 1 / #10 answer 5: new ClientHellos are rate-limited (whole transport) BEFORE any work; the
    // stateless cookie gate then answers unverified ones without allocating per-peer state (KI-30).
    std::uint32_t max_new_handshakes_per_s = 64;   // strangers (shared), and cookie-proven (shared, separate)
    std::uint32_t max_reconnect_hellos_per_s = 20; // per address with an established session (reserved)
    std::function<std::uint64_t()> unix_clock;               // wall-clock seconds; default std::time
    // KI-35 diagnostics: handed to every DTLS session (see DtlsConfig::trace). No secrets.
    std::function<void(const char* event, std::uint64_t now_us, std::uint32_t peer, std::string_view detail)> trace;
};

struct SecureStats {
    std::uint64_t pending_opened = 0, pending_refused_cap = 0, pending_failed = 0;
    std::uint64_t pending_replaced = 0;   // an unverified (or cookie-bearing) new handshake displaced a pending one
    std::uint64_t hello_rate_limited = 0; // new ClientHello dropped by max_new_handshakes_per_s (before any work)
    std::uint64_t hello_verify_sent = 0;  // stateless HelloVerifyRequests: ClientHellos without a valid cookie
    std::uint64_t authenticated = 0, promotions_replacing = 0, rejected_established = 0, idle_timeouts = 0;
    std::uint64_t dropped_not_dtls = 0;      // plaintext / non-ClientHello from a peer without a session
    std::uint64_t dropped_unknown_peer = 0;  // client side: datagram from a peer we never connected to
    std::uint64_t reliable_retransmits = 0, reliable_duplicates = 0;
    // DTLS handshakes that completed (promoted) or failed: how long they took and how many retransmission
    // timeouts they needed (diagnostics for reconnect latency, EXP-022).
    std::uint64_t handshakes_done = 0, handshake_retransmits = 0, handshake_ms_total = 0, handshake_ms_max = 0;
};

// Wraps `inner` (already constructed, not yet opened). Returns nullptr with *error when this build has no DTLS
// (MORDOR_WITH_GNS=OFF) or the config is unusable: callers must then FAIL, never fall back to plaintext.
std::unique_ptr<Transport> make_secure_transport(std::unique_ptr<Transport> inner, const SecureTransportConfig& cfg,
                                                 std::string* error);

// The decorator's own counters, or nullptr if `t` is not a secure transport.
const SecureStats* secure_stats(const Transport& t);

}  // namespace mordor::net
