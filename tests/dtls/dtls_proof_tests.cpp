// D-013 proof test (review #5 FB-5-05; the owner approved this test with the vcpkg OpenSSL on 2026-10-24).
// Question: does DTLS 1.2 in OpenSSL 3.6.5 work with RFC 7250 raw public keys, pinned out of band, and do the
// failure paths fail? Both peers run in-process over datagram-preserving memory BIOs (BIO_s_dgram_mem): no
// sockets, no network. This proves library behaviour only; it is not an integration with the Transport.
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "test_support.hpp"

namespace {

constexpr const char* kCiphers = "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES256-GCM-SHA384";
constexpr const char* kExporterLabel = "EXPORTER-mordor-invitation";

struct PKey {
    EVP_PKEY* p = nullptr;
    PKey() = default;
    explicit PKey(EVP_PKEY* k) : p(k) {}
    PKey(PKey&& o) noexcept : p(o.p) { o.p = nullptr; }
    PKey& operator=(PKey&&) = delete;
    ~PKey() { EVP_PKEY_free(p); }
};

PKey generate() {
    PKey k(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256"));
    if (!k.p) throw mordor::test::CheckFailure("EC key generation failed");
    return k;
}

std::vector<unsigned char> spki(EVP_PKEY* k) {  // DER SubjectPublicKeyInfo: what peers exchange out of band
    const int n = i2d_PUBKEY(k, nullptr);
    std::vector<unsigned char> out(static_cast<std::size_t>(n));
    unsigned char* p = out.data();
    i2d_PUBKEY(k, &p);
    return out;
}

std::string fingerprint(EVP_PKEY* k) {  // SHA-256 of the SPKI: the value two players confirm to each other
    const auto der = spki(k);
    std::array<unsigned char, 32> md{};
    unsigned int len = 0;
    EVP_Digest(der.data(), der.size(), md.data(), &len, EVP_sha256(), nullptr);
    std::string hex;
    char b[3];
    for (unsigned i = 0; i < len; ++i) {
        std::snprintf(b, sizeof b, "%02x", md[i]);
        hex += b;
    }
    return hex;
}

PKey public_only(EVP_PKEY* k) {  // the peer's view: the public key alone, rebuilt from the pinned SPKI
    const auto der = spki(k);
    const unsigned char* p = der.data();
    return PKey(d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size())));
}

struct Opts {
    int min_version = DTLS1_2_VERSION, max_version = DTLS1_2_VERSION;
    const char* ciphers = kCiphers;
    bool raw_public_keys = true;
};

struct Peer {
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;
    BIO* in = nullptr;   // datagrams arriving for this peer (owned by ssl)
    BIO* out = nullptr;  // datagrams this peer sends (owned by ssl)
    Peer(bool server, EVP_PKEY* own, EVP_PKEY* expected_peer, const Opts& o) {
        ctx = SSL_CTX_new(DTLS_method());
        CHECK(ctx);
        CHECK(SSL_CTX_set_min_proto_version(ctx, o.min_version) == 1);
        CHECK(SSL_CTX_set_max_proto_version(ctx, o.max_version) == 1);
        CHECK(SSL_CTX_set_cipher_list(ctx, o.ciphers) == 1);
        if (o.raw_public_keys) {
            const unsigned char rpk[] = {TLSEXT_cert_type_rpk};  // RPK only: X.509 is not offered or accepted
            CHECK(SSL_CTX_set1_client_cert_type(ctx, rpk, sizeof rpk) == 1);
            CHECK(SSL_CTX_set1_server_cert_type(ctx, rpk, sizeof rpk) == 1);
        }
        CHECK(SSL_CTX_use_PrivateKey(ctx, own) == 1);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
        // SSL_add_expected_rpk stores the pin as a DANE-EE SPKI record, so DANE must be on (first run: every
        // test failed here without it, evidence/logs/tests-dtls-first-run-2026-10-24.json).
        CHECK(SSL_CTX_dane_enable(ctx) == 1);
        ssl = SSL_new(ctx);
        CHECK(ssl);
        CHECK(SSL_dane_enable(ssl, nullptr) == 1);  // no host name: identity is the pinned key only
        if (expected_peer) CHECK(SSL_add_expected_rpk(ssl, expected_peer) == 1);  // the pin
        SSL_set_options(ssl, SSL_OP_NO_QUERY_MTU);
        CHECK(DTLS_set_link_mtu(ssl, 1200) == 1);
        in = BIO_new(BIO_s_dgram_mem());
        out = BIO_new(BIO_s_dgram_mem());
        CHECK(in && out);
        SSL_set_bio(ssl, in, out);
        if (server) SSL_set_accept_state(ssl);
        else SSL_set_connect_state(ssl);
    }
    ~Peer() {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
    }
    Peer(const Peer&) = delete;
    Peer& operator=(const Peer&) = delete;
};

// Moves every pending datagram from a to b; optionally keeps copies (to replay or tamper with them).
void shuttle(Peer& a, Peer& b, std::vector<std::vector<unsigned char>>* capture = nullptr) {
    std::array<unsigned char, 4096> buf{};
    for (int n = BIO_read(a.out, buf.data(), static_cast<int>(buf.size())); n > 0;
         n = BIO_read(a.out, buf.data(), static_cast<int>(buf.size()))) {
        if (capture) capture->emplace_back(buf.begin(), buf.begin() + n);
        CHECK(BIO_write(b.in, buf.data(), n) == n);
    }
}

void inject(Peer& to, const std::vector<unsigned char>& datagram) {
    CHECK(BIO_write(to.in, datagram.data(), static_cast<int>(datagram.size())) == static_cast<int>(datagram.size()));
}

bool fatal(SSL* s, int rc) {
    const int e = SSL_get_error(s, rc);
    return rc <= 0 && e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE;
}

std::string drain_errors() {
    std::string all;
    for (unsigned long e = ERR_get_error(); e; e = ERR_get_error()) {
        char b[256];
        ERR_error_string_n(e, b, sizeof b);
        all += std::string(all.empty() ? "" : "; ") + b;
    }
    return all;
}

// Runs both handshakes to completion or failure. Returns true only if BOTH sides completed.
bool handshake(Peer& client, Peer& server, std::string* why = nullptr) {
    for (int round = 0; round < 50; ++round) {
        const int c = SSL_do_handshake(client.ssl);
        if (fatal(client.ssl, c)) {
            shuttle(client, server);  // let the alert reach the other side
            SSL_do_handshake(server.ssl);
            if (why) *why = "client: " + drain_errors();
            return false;
        }
        shuttle(client, server);
        const int s = SSL_do_handshake(server.ssl);
        if (fatal(server.ssl, s)) {
            shuttle(server, client);
            SSL_do_handshake(client.ssl);
            if (why) *why = "server: " + drain_errors();
            return false;
        }
        shuttle(server, client);
        if (c == 1 && s == 1) return true;
    }
    if (why) *why = "no progress";
    return false;
}

std::string read_all(Peer& p) {
    std::array<char, 2048> buf{};
    const int n = SSL_read(p.ssl, buf.data(), static_cast<int>(buf.size()));
    return n > 0 ? std::string(buf.data(), static_cast<std::size_t>(n)) : std::string();
}

std::vector<unsigned char> exporter(Peer& p, const std::string& invitation) {
    std::vector<unsigned char> out(32);
    CHECK(SSL_export_keying_material(p.ssl, out.data(), out.size(), kExporterLabel, std::strlen(kExporterLabel),
                                     reinterpret_cast<const unsigned char*>(invitation.data()), invitation.size(), 1) == 1);
    return out;
}

struct Pair {
    PKey host_key = generate(), client_key = generate();
    PKey host_pub = public_only(host_key.p), client_pub = public_only(client_key.p);
};

}  // namespace

TEST(dtls12_with_pinned_raw_public_keys_succeeds) {
    Pair k;
    Peer host(true, k.host_key.p, k.client_pub.p, {});
    Peer client(false, k.client_key.p, k.host_pub.p, {});
    std::string why;
    CHECK(handshake(client, host, &why));
    CHECK(SSL_version(client.ssl) == DTLS1_2_VERSION && SSL_version(host.ssl) == DTLS1_2_VERSION);
    // Raw public keys negotiated in BOTH directions; no X.509 certificate anywhere.
    CHECK(SSL_get_negotiated_server_cert_type(client.ssl) == TLSEXT_cert_type_rpk);
    CHECK(SSL_get_negotiated_client_cert_type(host.ssl) == TLSEXT_cert_type_rpk);
    CHECK(SSL_get0_peer_certificate(client.ssl) == nullptr && SSL_get0_peer_certificate(host.ssl) == nullptr);
    CHECK(SSL_get_verify_result(client.ssl) == X509_V_OK && SSL_get_verify_result(host.ssl) == X509_V_OK);
    CHECK(fingerprint(SSL_get0_peer_rpk(client.ssl)) == fingerprint(k.host_key.p));    // client sees the pinned host
    CHECK(fingerprint(SSL_get0_peer_rpk(host.ssl)) == fingerprint(k.client_key.p));    // host sees the pinned client
    const std::string cipher = SSL_get_cipher_name(client.ssl);
    CHECK(cipher.rfind("ECDHE-", 0) == 0);                                              // forward secrecy
    CHECK(cipher.find("GCM") != std::string::npos || cipher.find("CHACHA20") != std::string::npos);  // AEAD
    CHECK(SSL_write(client.ssl, "hello", 5) == 5);
    shuttle(client, host);
    CHECK(read_all(host) == "hello");
    std::printf("       %s, cipher %s, host key %s...\n", SSL_get_version(client.ssl), cipher.c_str(),
                fingerprint(k.host_key.p).substr(0, 16).c_str());
}

TEST(dtls12_wrong_host_key_is_rejected_by_client) {  // a relay/MITM presenting its own key
    Pair k;
    PKey mitm = generate();
    Peer impostor(true, mitm.p, k.client_pub.p, {});
    Peer client(false, k.client_key.p, k.host_pub.p, {});
    std::string why;
    CHECK(!handshake(client, impostor, &why));
    CHECK(SSL_get_verify_result(client.ssl) != X509_V_OK);
    std::printf("       rejected: %s\n", why.c_str());
}

TEST(dtls12_wrong_client_key_is_rejected_by_host) {
    Pair k;
    PKey stranger = generate();
    Peer host(true, k.host_key.p, k.client_pub.p, {});
    Peer client(false, stranger.p, k.host_pub.p, {});
    std::string why;
    CHECK(!handshake(client, host, &why));
    CHECK(SSL_get_verify_result(host.ssl) != X509_V_OK);
    std::printf("       rejected: %s\n", why.c_str());
}

TEST(dtls12_peer_without_raw_public_key_is_rejected) {  // downgrade attempt: no RPK offered (X.509 path)
    Pair k;
    Opts no_rpk;
    no_rpk.raw_public_keys = false;
    Peer host(true, k.host_key.p, k.client_pub.p, {});
    Peer client(false, k.client_key.p, nullptr, no_rpk);
    std::string why;
    CHECK(!handshake(client, host, &why));
    std::printf("       rejected: %s\n", why.c_str());
}

TEST(dtls_older_protocol_version_is_rejected) {  // downgrade attempt: DTLS 1.0 only
    Pair k;
    Opts old;
    old.min_version = old.max_version = DTLS1_VERSION;
    Peer host(true, k.host_key.p, k.client_pub.p, {});
    Peer client(false, k.client_key.p, k.host_pub.p, old);
    std::string why;
    CHECK(!handshake(client, host, &why));
    CHECK(SSL_is_init_finished(host.ssl) == 0);
    std::printf("       rejected: %s\n", why.c_str());
}

TEST(dtls12_non_aead_cipher_suite_is_rejected) {  // downgrade attempt: ECDHE but CBC (not AEAD)
    Pair k;
    Opts cbc;
    cbc.ciphers = "ECDHE-ECDSA-AES128-SHA";
    Peer host(true, k.host_key.p, k.client_pub.p, {});
    Peer client(false, k.client_key.p, k.host_pub.p, cbc);
    std::string why;
    CHECK(!handshake(client, host, &why));
    std::printf("       rejected: %s\n", why.c_str());
}

// Session binding: both sides derive the same value for the same invitation, a different one for another
// invitation, and a fresh one for every new handshake (fresh ECDHE: a recorded session's value is useless).
TEST(dtls12_exporter_binds_invitation_and_is_fresh_per_handshake) {
    Pair k;
    std::vector<unsigned char> first;
    for (int run = 0; run < 2; ++run) {
        Peer host(true, k.host_key.p, k.client_pub.p, {});
        Peer client(false, k.client_key.p, k.host_pub.p, {});
        CHECK(handshake(client, host));
        const auto a = exporter(client, "invite-0001");
        CHECK(a == exporter(host, "invite-0001"));
        CHECK(a != exporter(host, "invite-0002"));
        if (run == 0) first = a;
        else CHECK(a != first);
    }
}

TEST(dtls12_replayed_and_tampered_records_are_dropped) {
    Pair k;
    Peer host(true, k.host_key.p, k.client_pub.p, {});
    Peer client(false, k.client_key.p, k.host_pub.p, {});
    CHECK(handshake(client, host));
    std::vector<std::vector<unsigned char>> sent;
    CHECK(SSL_write(client.ssl, "move-1", 6) == 6);
    shuttle(client, host, &sent);
    CHECK(sent.size() == 1);
    CHECK(read_all(host) == "move-1");
    inject(host, sent[0]);                 // replay of the same record
    CHECK(read_all(host).empty());         // dropped by the DTLS replay window
    auto bad = sent[0];
    bad.back() ^= 0x01;                    // tampered record (breaks the AEAD tag)
    inject(host, bad);
    CHECK(read_all(host).empty());         // dropped
    CHECK(SSL_write(client.ssl, "move-2", 6) == 6);
    shuttle(client, host);
    CHECK(read_all(host) == "move-2");     // the channel survives both
}
