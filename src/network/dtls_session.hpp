#pragma once
// DTLS 1.2 + RFC 7250 raw public keys over Transport::Unreliable (D-013 option A; review #6 FB-6-01/FB-6-02).
// One session per peer connection. DTLS records travel as plain Unreliable transport payloads on ANY backend;
// OpenSSL's own timers drive retransmission (DTLSv1_get_timeout / DTLSv1_handle_timeout).
//
// Fail-closed invitation binding (FB-6-01): after the DTLS handshake each side sends, inside the channel, the
// invitation it holds (id, protocol version, expiry) plus a confirmation value = TLS exporter (RFC 5705) over
// that invitation. The receiver accepts only if every field equals its own invitation, its own invitation has
// not expired, and the confirmation matches its own exporter value (constant-time compare). Until then no
// application data is accepted (counted and dropped). No custom cryptography: identity is the pinned key,
// freshness and binding come from DTLS and its exporter.
//
// Status: research prototype behind the optional GNS build (OpenSSL). Not integrated with Host/Client yet, not
// independently reviewed: loopback / trusted lab only (D-013, KI-29).
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "network/protocol.hpp"
#include "network/secure_transport.hpp"  // Invitation
#include "network/transport.hpp"

typedef struct evp_pkey_st EVP_PKEY;
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct bio_st BIO;

namespace mordor::net {

struct DtlsConfig {
    bool server = false;
    EVP_PKEY* own_key = nullptr;               // private key (not owned)
    // Pinned peer public keys (DER SubjectPublicKeyInfo). A host lists every invited player; any one may connect.
    std::vector<std::vector<std::uint8_t>> pinned_peers;
    Invitation invitation;
    std::size_t mtu = 1200;                    // capped at the transport's max_payload
    std::uint64_t handshake_timeout_us = 15'000'000;
    std::uint64_t binding_timeout_us = 5'000'000;
    std::uint64_t binding_resend_us = 200'000;
    // Handshake retransmission back-off, applied through OpenSSL's own DTLS_set_timer_cb (review #7 FB-7-02).
    // OpenSSL's default (1 s doubling to 60 s) allowed only ~4 attempts in a 15 s budget: too few for 25% loss.
    std::uint32_t retransmit_initial_us = 250'000;
    // 1 s, not 2 s: a reconnect under the harsh profile hit 2 s waits after a few lost flights, and the secure
    // stall recovery exceeded the soak's 3 s gate in half of the 30 s runs (EXP-022).
    std::uint32_t retransmit_max_us = 1'000'000;
    bool send_binding = true;                  // test hook only: false simulates a peer that never confirms
    bool corrupt_confirmation = false;         // test hook only: flips one bit of the confirmation we send
    bool coalesce_records = true;              // KI-35 / FB-11-05: false restores OpenSSL's own packetization
    bool cookie_exchange = true;               // server: HelloVerifyRequest round trip (off only where the inner
                                               // transport already proves return routability, FB-15-01)
    // EXP-043 experiment (FB-28-01), default off; both peers must enable it. The host (server) sends a BindingAck
    // when it verifies the client's binding, which is the moment it starts accepting application data (the host
    // never waits). The client becomes Authenticated only once it has verified the host's binding AND received that
    // ack, so its first application record cannot reach a host that still drops it (EXP-042). Gating the host as
    // well let a host ack while it still waited, and the client's record was dropped (FB-28-02 test). Without an
    // ack after confirm_timeout_us the client proceeds as without the flag (gate_timeouts): never a hang with a
    // host that lacks it. Fail-closed checks are unchanged; nothing is buffered.
    bool confirm_binding = false;
    std::uint64_t confirm_timeout_us = 2'000'000;
    // KI-35 diagnostics (review #10): called for every datagram and milestone BEFORE Authenticated. No secrets:
    // `detail` lists records as content_type/epoch[/handshake_type], or names a milestone / reject reason.
    std::function<void(const char* event, std::uint64_t now_us, std::uint32_t peer, std::string_view detail)> trace;
};

enum class DtlsState { Handshaking, Binding, Authenticated, Rejected };
const char* to_string(DtlsState s);

struct DtlsStats {
    std::uint64_t datagrams_in = 0, datagrams_out = 0, send_failures = 0;
    std::uint64_t timeouts_handled = 0;          // OpenSSL retransmission timer fired
    std::uint64_t cookies_issued = 0, cookies_verified = 0;  // server: HelloVerifyRequest exchange
    std::uint64_t binding_sent = 0, binding_received = 0;
    std::uint64_t binding_replies_suppressed = 0;  // rate limit: at most one reply per binding_resend_us (FB-7-04)
    std::uint64_t app_dropped_before_auth = 0;   // fail-closed: application data before Authenticated
    std::uint64_t binding_acks_sent = 0, binding_acks_received = 0, gate_timeouts = 0;  // confirm_binding (EXP-043)
    std::uint64_t app_in = 0, app_out = 0;
    std::uint64_t datagrams_coalesced = 0;  // BIO datagrams merged into the previous one (KI-35)
    std::size_t max_datagram_out = 0;  // never above min(mtu, transport max_payload)
};

class DtlsSession {
public:
    DtlsSession(Transport& transport, PeerId peer, Endpoint remote, DtlsConfig cfg);

    // Stateless cookie gate (KI-30, review #10 answer 5). A SERVER session used as the decorator's single listener:
    // listen_offer() feeds one datagram from (peer, remote) to DTLSv1_listen. Without a valid cookie it answers with
    // a HelloVerifyRequest and keeps NO per-peer state; with one it returns true, and this object is then bound to
    // that peer and continues the handshake as an ordinary pending session.
    bool listen_offer(std::span<const std::uint8_t> datagram, PeerId peer, Endpoint remote, std::uint64_t now_us);
    ~DtlsSession();
    DtlsSession(const DtlsSession&) = delete;
    DtlsSession& operator=(const DtlsSession&) = delete;

    // now_unix_s is wall-clock seconds, used only for the invitation expiry check.
    void on_datagram(std::span<const std::uint8_t> bytes, std::uint64_t now_us, std::uint64_t now_unix_s);
    void service(std::uint64_t now_us, std::uint64_t now_unix_s);  // timers, retransmits, binding resends
    // Failed unless Authenticated; otherwise the inner transport's own result for the datagram(s) (Codex pass 6,
    // PR #24: a local refusal such as Backpressure or NotConnected is reported, never hidden as Sent).
    // now_us (0 = the last time seen) reaches the transport: a backend that simulates latency in our code (UDP
    // ImpairedSender) schedules by it (EXP-021: passing 0 disabled latency and jitter).
    SendResult send(std::span<const std::uint8_t> bytes, std::uint64_t now_us = 0);
    std::vector<std::vector<std::uint8_t>> take_received();

    DtlsState state() const { return state_; }
    const std::string& reason() const { return reason_; }
    const DtlsStats& stats() const { return stats_; }
    PeerId peer() const { return peer_; }
    std::string peer_fingerprint() const;                   // SHA-256 hex of the peer's SPKI, once known
    std::vector<std::uint8_t> confirmation() const { return confirmation_; }  // fresh per handshake

    // Internal (OpenSSL cookie callbacks).
    bool cookie_generate(unsigned char* out, unsigned int* len);
    bool cookie_verify(const unsigned char* cookie, unsigned int len);
    bool cookie_matches(std::span<const std::uint8_t> cookie) const;  // same check, without counting
    // Listener only: would `cookie` verify for a ClientHello from (peer, remote)? Rebinds the listener to them.
    bool cookie_valid_for(PeerId peer, Endpoint remote, std::span<const std::uint8_t> cookie, std::uint64_t now_us);
    unsigned int next_retransmit_us(unsigned int previous_us) const;

private:
    void drive(std::uint64_t now_us, std::uint64_t now_unix_s);
    void read_records(std::uint64_t now_us, std::uint64_t now_unix_s);
    void on_binding(std::span<const std::uint8_t> msg, std::uint64_t now_unix_s);
    void send_binding(std::uint64_t now_us);
    void send_binding_ack();
    void maybe_authenticate(bool gate_expired);
    bool gates() const { return cfg_.confirm_binding && !cfg_.server; }  // the client waits for the host's ack
    bool acks() const { return cfg_.confirm_binding && cfg_.server; }    // the host acks the client's binding
    SendResult flush_out();  // Sent, or the first local refusal of a datagram
    void reject(std::string why);
    bool make_cookie(unsigned char* out, unsigned int* len, unsigned slots_back = 0) const;

    Transport& transport_;
    PeerId peer_;
    Endpoint remote_;
    DtlsConfig cfg_;
    SSL_CTX* ctx_ = nullptr;
    SSL* ssl_ = nullptr;
    BIO* in_ = nullptr;
    BIO* out_ = nullptr;
    DtlsState state_ = DtlsState::Handshaking;
    std::string reason_;
    DtlsStats stats_;
    std::uint64_t started_us_ = 0, binding_started_us_ = 0, last_binding_us_ = 0, peer_bound_us_ = 0;
    std::uint64_t now_us_ = 0;  // latest time passed in; every datagram is sent with it
    bool started_ = false, peer_bound_ = false, peer_acked_ = false;  // peer_acked_: confirm_binding only
    std::vector<std::uint8_t> confirmation_;
    std::vector<std::vector<std::uint8_t>> received_;
};

// True for a plaintext (epoch 0) DTLS ClientHello record. On a connectionless backend a reconnecting client
// arrives on the SAME PeerId; the host then runs a second, pending session next to the established one and
// promotes it only once it authenticates, so a spoofed ClientHello cannot tear down a valid session.
bool dtls_is_client_hello(std::span<const std::uint8_t> datagram);

// Fields of a complete (unfragmented) epoch-0 ClientHello, or nullopt. The client random identifies one handshake
// attempt (retransmissions and the cookie-bearing second ClientHello keep it); the cookie proves the sender
// received our HelloVerifyRequest at that address (review #8 FB-8-01).
struct ClientHelloInfo {
    std::array<std::uint8_t, 32> random{};
    std::vector<std::uint8_t> cookie;
};
std::optional<ClientHelloInfo> dtls_parse_client_hello(std::span<const std::uint8_t> datagram);

// Helpers for pinning: DER SubjectPublicKeyInfo and its SHA-256 fingerprint (hex).
std::vector<std::uint8_t> spki_der(EVP_PKEY* key);
std::string spki_fingerprint(std::span<const std::uint8_t> spki);

}  // namespace mordor::net
