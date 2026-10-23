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
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "network/protocol.hpp"
#include "network/transport.hpp"

typedef struct evp_pkey_st EVP_PKEY;
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct bio_st BIO;

namespace mordor::net {

struct Invitation {
    std::string id;                    // given out of band with the host's key fingerprint
    std::uint16_t protocol_version = kProtocolVersion;
    std::uint64_t expires_unix_s = 0;  // wall clock; the invitation is refused after this
    auto operator<=>(const Invitation&) const = default;
};

struct DtlsConfig {
    bool server = false;
    EVP_PKEY* own_key = nullptr;               // private key (not owned)
    std::vector<std::uint8_t> peer_spki;       // pinned peer public key, DER SubjectPublicKeyInfo
    Invitation invitation;
    std::size_t mtu = 1200;                    // capped at the transport's max_payload
    std::uint64_t handshake_timeout_us = 15'000'000;
    std::uint64_t binding_timeout_us = 5'000'000;
    std::uint64_t binding_resend_us = 200'000;
    bool send_binding = true;                  // test hook only: false simulates a peer that never confirms
};

enum class DtlsState { Handshaking, Binding, Authenticated, Rejected };
const char* to_string(DtlsState s);

struct DtlsStats {
    std::uint64_t datagrams_in = 0, datagrams_out = 0, send_failures = 0;
    std::uint64_t timeouts_handled = 0;          // OpenSSL retransmission timer fired
    std::uint64_t cookies_issued = 0, cookies_verified = 0;  // server: HelloVerifyRequest exchange
    std::uint64_t binding_sent = 0, binding_received = 0;
    std::uint64_t app_dropped_before_auth = 0;   // fail-closed: application data before Authenticated
    std::uint64_t app_in = 0, app_out = 0;
    std::size_t max_datagram_out = 0;  // never above min(mtu, transport max_payload)
};

class DtlsSession {
public:
    DtlsSession(Transport& transport, PeerId peer, Endpoint remote, DtlsConfig cfg);
    ~DtlsSession();
    DtlsSession(const DtlsSession&) = delete;
    DtlsSession& operator=(const DtlsSession&) = delete;

    // now_unix_s is wall-clock seconds, used only for the invitation expiry check.
    void on_datagram(std::span<const std::uint8_t> bytes, std::uint64_t now_us, std::uint64_t now_unix_s);
    void service(std::uint64_t now_us, std::uint64_t now_unix_s);  // timers, retransmits, binding resends
    bool send(std::span<const std::uint8_t> bytes);                // false unless Authenticated
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

private:
    void drive(std::uint64_t now_us, std::uint64_t now_unix_s);
    void read_records(std::uint64_t now_us, std::uint64_t now_unix_s);
    void on_binding(std::span<const std::uint8_t> msg, std::uint64_t now_unix_s);
    void send_binding(std::uint64_t now_us);
    void flush_out();
    void reject(std::string why);
    bool make_cookie(unsigned char* out, unsigned int* len) const;

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
    std::uint64_t started_us_ = 0, binding_started_us_ = 0, last_binding_us_ = 0;
    bool started_ = false, peer_bound_ = false;
    std::vector<std::uint8_t> confirmation_;
    std::vector<std::vector<std::uint8_t>> received_;
};

// True for a plaintext (epoch 0) DTLS ClientHello record. On a connectionless backend a reconnecting client
// arrives on the SAME PeerId; the host then runs a second, pending session next to the established one and
// promotes it only once it authenticates, so a spoofed ClientHello cannot tear down a valid session.
bool dtls_is_client_hello(std::span<const std::uint8_t> datagram);

// Helpers for pinning: DER SubjectPublicKeyInfo and its SHA-256 fingerprint (hex).
std::vector<std::uint8_t> spki_der(EVP_PKEY* key);
std::string spki_fingerprint(std::span<const std::uint8_t> spki);

}  // namespace mordor::net
