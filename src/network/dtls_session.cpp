#include "network/dtls_session.hpp"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace mordor::net {

namespace {

constexpr const char* kCiphers = "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES256-GCM-SHA384";
constexpr const char* kBindingLabel = "EXPORTER-mordor-invitation-binding";
constexpr std::uint8_t kTypeBinding = 0xB1, kTypeApp = 0xA1;
constexpr std::uint8_t kTypeBindingAck = 0xB2;  // confirm_binding only (EXP-043): "I verified your binding"
constexpr std::size_t kConfirmationSize = 32;

// Per-process random key for stateless-style cookies: HMAC(key, peer id/generation + remote endpoint).
constexpr std::uint64_t kCookieSlotUs = 30'000'000;

const std::array<unsigned char, 32>& cookie_key() {
    static const auto k = [] {
        std::array<unsigned char, 32> key{};
        RAND_bytes(key.data(), static_cast<int>(key.size()));
        return key;
    }();
    return k;
}

int cookie_generate_cb(SSL* ssl, unsigned char* cookie, unsigned int* len) {
    auto* self = static_cast<DtlsSession*>(SSL_get_app_data(ssl));
    return self && self->cookie_generate(cookie, len) ? 1 : 0;
}
int cookie_verify_cb(SSL* ssl, const unsigned char* cookie, unsigned int len) {
    auto* self = static_cast<DtlsSession*>(SSL_get_app_data(ssl));
    return self && self->cookie_verify(cookie, len) ? 1 : 0;
}

unsigned int timer_cb(SSL* ssl, unsigned int previous_us) {
    auto* self = static_cast<DtlsSession*>(SSL_get_app_data(ssl));
    return self ? self->next_retransmit_us(previous_us) : 1'000'000;
}

std::string last_errors() {
    std::string all;
    for (unsigned long e = ERR_get_error(); e; e = ERR_get_error()) {
        char b[256];
        ERR_error_string_n(e, b, sizeof b);
        all += std::string(all.empty() ? "" : "; ") + b;
    }
    return all;
}

// "22/0/1,20/0,22/1": content type / epoch [/ handshake type when readable] of each record in a datagram.
std::string describe_records(std::span<const std::uint8_t> b) {
    std::string s;
    for (std::size_t off = 0; off + 13 <= b.size();) {
        const unsigned epoch = (b[off + 3] << 8) | b[off + 4];
        const std::size_t len = (b[off + 11] << 8) | b[off + 12];
        if (!s.empty()) s += ',';
        s += std::to_string(b[off]) + '/' + std::to_string(epoch);
        if (b[off] == 22 && epoch == 0 && off + 13 < b.size()) s += '/' + std::to_string(b[off + 13]);
        off += 13 + len;
    }
    return s;
}

void put_u16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}
void put_u64(std::vector<std::uint8_t>& b, std::uint64_t v) {
    for (int s = 56; s >= 0; s -= 8) b.push_back(static_cast<std::uint8_t>(v >> s));
}

std::vector<std::uint8_t> encode_invitation(const Invitation& inv) {  // exporter context and wire fields
    std::vector<std::uint8_t> b;
    put_u16(b, inv.protocol_version);
    put_u64(b, inv.expires_unix_s);
    b.push_back(static_cast<std::uint8_t>(std::min<std::size_t>(inv.id.size(), 255)));
    b.insert(b.end(), inv.id.begin(), inv.id.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(inv.id.size(), 255)));
    return b;
}

std::optional<Invitation> decode_invitation(std::span<const std::uint8_t> b, std::size_t& used) {
    if (b.size() < 11) return std::nullopt;
    Invitation inv;
    inv.protocol_version = static_cast<std::uint16_t>((b[0] << 8) | b[1]);
    inv.expires_unix_s = 0;
    for (int i = 0; i < 8; ++i) inv.expires_unix_s = (inv.expires_unix_s << 8) | b[2 + i];
    const std::size_t n = b[10];
    if (b.size() < 11 + n) return std::nullopt;
    inv.id.assign(reinterpret_cast<const char*>(b.data() + 11), n);
    used = 11 + n;
    return inv;
}

}  // namespace

const char* to_string(DtlsState s) {
    switch (s) {
        case DtlsState::Handshaking: return "handshaking";
        case DtlsState::Binding: return "binding";
        case DtlsState::Authenticated: return "authenticated";
        case DtlsState::Rejected: return "rejected";
    }
    return "?";
}

bool dtls_is_client_hello(std::span<const std::uint8_t> d) {
    // record: type(1)=22 handshake, version(2), epoch(2)=0, seq(6), length(2); handshake msg_type at 13 = 1
    return d.size() > 13 && d[0] == 22 && d[3] == 0 && d[4] == 0 && d[13] == 1;
}

std::optional<ClientHelloInfo> dtls_parse_client_hello(std::span<const std::uint8_t> d) {
    if (!dtls_is_client_hello(d) || d.size() < 60) return std::nullopt;
    auto u24 = [&](std::size_t o) { return (std::size_t{d[o]} << 16) | (std::size_t{d[o + 1]} << 8) | d[o + 2]; };
    const std::size_t length = u24(14), frag_off = u24(19), frag_len = u24(22);
    if (frag_off != 0 || frag_len != length || d.size() < 25 + length) return std::nullopt;  // complete only
    ClientHelloInfo ch;
    std::copy(d.begin() + 27, d.begin() + 59, ch.random.begin());
    std::size_t p = 59;
    const std::size_t sid = d[p++];
    p += sid;
    if (p >= d.size()) return std::nullopt;
    const std::size_t cookie_len = d[p++];
    if (p + cookie_len > d.size()) return std::nullopt;
    ch.cookie.assign(d.begin() + static_cast<std::ptrdiff_t>(p), d.begin() + static_cast<std::ptrdiff_t>(p + cookie_len));
    return ch;
}

std::vector<std::uint8_t> spki_der(EVP_PKEY* key) {
    const int n = i2d_PUBKEY(key, nullptr);
    if (n <= 0) return {};
    std::vector<std::uint8_t> out(static_cast<std::size_t>(n));
    unsigned char* p = out.data();
    i2d_PUBKEY(key, &p);
    return out;
}

std::string spki_fingerprint(std::span<const std::uint8_t> spki) {
    std::array<unsigned char, 32> md{};
    unsigned int len = 0;
    EVP_Digest(spki.data(), spki.size(), md.data(), &len, EVP_sha256(), nullptr);
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (unsigned i = 0; i < len; ++i) {
        s += hex[md[i] >> 4];
        s += hex[md[i] & 15];
    }
    return s;
}

DtlsSession::DtlsSession(Transport& transport, PeerId peer, Endpoint remote, DtlsConfig cfg)
    : transport_(transport), peer_(peer), remote_(remote), cfg_(std::move(cfg)) {
    ERR_clear_error();
    auto fail = [&](const char* what) { reject(std::string("setup: ") + what + " " + last_errors()); };
    ctx_ = SSL_CTX_new(DTLS_method());
    const unsigned char rpk[] = {TLSEXT_cert_type_rpk};  // raw public keys only; X.509 is neither offered nor accepted
    if (!ctx_ || cfg_.pinned_peers.empty() || !cfg_.own_key || SSL_CTX_set_min_proto_version(ctx_, DTLS1_2_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(ctx_, DTLS1_2_VERSION) != 1 || SSL_CTX_set_cipher_list(ctx_, kCiphers) != 1 ||
        SSL_CTX_set1_client_cert_type(ctx_, rpk, sizeof rpk) != 1 || SSL_CTX_set1_server_cert_type(ctx_, rpk, sizeof rpk) != 1 ||
        SSL_CTX_use_PrivateKey(ctx_, cfg_.own_key) != 1 || SSL_CTX_dane_enable(ctx_) != 1) {
        fail("context");
        return;
    }
    SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    if (cfg_.server && cfg_.cookie_exchange) {
        // HelloVerifyRequest cookies bound to this peer (review #6 answer 5).
        SSL_CTX_set_cookie_generate_cb(ctx_, cookie_generate_cb);
        SSL_CTX_set_cookie_verify_cb(ctx_, cookie_verify_cb);
        SSL_CTX_set_options(ctx_, SSL_OP_COOKIE_EXCHANGE);
    }
    SSL_CTX_set_options(ctx_, SSL_OP_NO_RENEGOTIATION);  // a new handshake is a new session, never in-place
    ssl_ = SSL_new(ctx_);
    // SSL_add_expected_rpk stores each pin as a DANE-EE SPKI record: DANE must be enabled first (EXP-017).
    if (!ssl_ || SSL_dane_enable(ssl_, nullptr) != 1) {
        fail("session");
        return;
    }
    for (const auto& der : cfg_.pinned_peers) {
        const unsigned char* p = der.data();
        EVP_PKEY* pin = d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size()));
        const bool ok = pin && SSL_add_expected_rpk(ssl_, pin) == 1;
        EVP_PKEY_free(pin);
        if (!ok) {
            fail("pin");
            return;
        }
    }
    SSL_set_app_data(ssl_, this);
    DTLS_set_timer_cb(ssl_, timer_cb);
    SSL_set_options(ssl_, SSL_OP_NO_QUERY_MTU);
    const auto mtu = std::min(cfg_.mtu, transport_.capabilities().max_payload);
    DTLS_set_link_mtu(ssl_, static_cast<long>(mtu));
    in_ = BIO_new(BIO_s_dgram_mem());
    out_ = BIO_new(BIO_s_dgram_mem());
    if (!in_ || !out_) {
        BIO_free(in_);
        BIO_free(out_);
        in_ = out_ = nullptr;
        fail("bio");
        return;
    }
    SSL_set_bio(ssl_, in_, out_);
    if (cfg_.server) SSL_set_accept_state(ssl_);
    else SSL_set_connect_state(ssl_);
}

DtlsSession::~DtlsSession() {
    SSL_free(ssl_);  // frees the BIOs
    SSL_CTX_free(ctx_);
}

bool DtlsSession::make_cookie(unsigned char* out, unsigned int* len, unsigned slots_back) const {
    // Bound to the peer, its address and a 30 s time slot, so a cookie cannot be replayed forever (independent
    // review of #10): the current and the previous slot verify, i.e. a cookie lives 30-60 s.
    const auto slot = static_cast<std::uint16_t>(now_us_ / kCookieSlotUs - slots_back);
    unsigned char msg[16];
    std::memcpy(msg, &peer_.id, 4);
    std::memcpy(msg + 4, &peer_.generation, 4);
    std::memcpy(msg + 8, &remote_.ip, 4);
    std::memcpy(msg + 12, &remote_.port, 2);
    std::memcpy(msg + 14, &slot, 2);
    std::size_t n = 0;
    unsigned char md[EVP_MAX_MD_SIZE];
    if (!EVP_Q_mac(nullptr, "HMAC", nullptr, "SHA256", nullptr, cookie_key().data(), cookie_key().size(), msg, sizeof msg,
                   md, sizeof md, &n))
        return false;
    std::memcpy(out, md, n);
    *len = static_cast<unsigned int>(n);
    return true;
}

unsigned int DtlsSession::next_retransmit_us(unsigned int previous_us) const {
    if (previous_us == 0) return cfg_.retransmit_initial_us;  // first flight
    return std::min<unsigned int>(previous_us * 2, cfg_.retransmit_max_us);
}

bool DtlsSession::cookie_generate(unsigned char* out, unsigned int* len) {
    if (!make_cookie(out, len)) return false;
    ++stats_.cookies_issued;
    return true;
}

bool DtlsSession::cookie_matches(std::span<const std::uint8_t> cookie) const {
    for (unsigned back = 0; back < 2; ++back) {
        unsigned char expect[EVP_MAX_MD_SIZE];
        unsigned int n = 0;
        if (make_cookie(expect, &n, back) && n == cookie.size() && CRYPTO_memcmp(expect, cookie.data(), n) == 0) return true;
    }
    return false;
}

bool DtlsSession::cookie_valid_for(PeerId peer, Endpoint remote, std::span<const std::uint8_t> cookie, std::uint64_t now_us) {
    peer_ = peer;
    remote_ = remote;
    now_us_ = now_us;
    return cookie_matches(cookie);
}

bool DtlsSession::cookie_verify(const unsigned char* cookie, unsigned int len) {
    if (!cookie_matches({cookie, len})) return false;
    ++stats_.cookies_verified;
    return true;
}

void DtlsSession::reject(std::string why) {
    if (state_ == DtlsState::Rejected) return;
    state_ = DtlsState::Rejected;
    reason_ = std::move(why);
    if (cfg_.trace) cfg_.trace("rejected", now_us_, peer_.id, reason_);
}

SendResult DtlsSession::flush_out() {
    if (!out_) return SendResult::Sent;
    // KI-35: OpenSSL packs a flight's first transmission into one datagram but RETRANSMITS it one datagram per
    // record, so a 5-record flight then needs 5 datagrams to all survive (0.85^5 = 44% per round at 15% loss).
    // Consecutive BIO datagrams are coalesced up to the MTU; several records per datagram is valid DTLS
    // (RFC 6347 4.1.1), and each BIO datagram holds whole records.
    const std::size_t mtu = std::min(cfg_.mtu, transport_.capabilities().max_payload);
    std::vector<std::uint8_t> pack;
    SendResult result = SendResult::Sent;
    auto send_pack = [&] {
        if (pack.empty()) return;
        const auto r = transport_.send(peer_, pack, Delivery::Unreliable, now_us_);
        if (result == SendResult::Sent) result = r;
        if (cfg_.trace && state_ != DtlsState::Authenticated)
            cfg_.trace(r == SendResult::Sent ? "tx" : "tx_failed", now_us_, peer_.id, describe_records(pack));
        stats_.max_datagram_out = std::max(stats_.max_datagram_out, pack.size());
        if (r == SendResult::Sent) ++stats_.datagrams_out;
        else ++stats_.send_failures;  // lost like any datagram: DTLS retransmits handshake flights itself
        pack.clear();
    };
    std::array<std::uint8_t, 4096> buf{};
    for (int n = BIO_read(out_, buf.data(), static_cast<int>(buf.size())); n > 0;
         n = BIO_read(out_, buf.data(), static_cast<int>(buf.size()))) {
        if (static_cast<std::size_t>(n) >= buf.size()) {  // BIO datagram reads truncate silently: never send one
            ++stats_.send_failures;
            if (result == SendResult::Sent) result = SendResult::Failed;
            continue;
        }
        if (!pack.empty() && (!cfg_.coalesce_records || pack.size() + static_cast<std::size_t>(n) > mtu)) send_pack();
        else if (!pack.empty()) ++stats_.datagrams_coalesced;
        pack.insert(pack.end(), buf.data(), buf.data() + n);
    }
    send_pack();
    return result;
}

bool DtlsSession::listen_offer(std::span<const std::uint8_t> datagram, PeerId peer, Endpoint remote, std::uint64_t now_us) {
    if (!ssl_ || !in_ || !cfg_.server || state_ != DtlsState::Handshaking) return false;
    peer_ = peer;      // the cookie is bound to this peer and address; the HelloVerifyRequest goes back to it
    remote_ = remote;
    now_us_ = now_us;
    ERR_clear_error();
    if (cfg_.trace) cfg_.trace("listen_rx", now_us, peer.id, describe_records(datagram));
    BIO_write(in_, datagram.data(), static_cast<int>(datagram.size()));
    BIO_ADDR* addr = BIO_ADDR_new();
    const int r = DTLSv1_listen(ssl_, addr);
    BIO_ADDR_free(addr);
    if (r < 0) {
        ERR_clear_error();  // a malformed datagram: drop it, the listener stays usable
        return false;
    }
    flush_out();  // the HelloVerifyRequest, if one was produced
    if (r == 0) return false;  // (cookies_verified was counted by the verify callback)
    started_ = true;
    started_us_ = now_us;
    return true;
}

void DtlsSession::on_datagram(std::span<const std::uint8_t> bytes, std::uint64_t now_us, std::uint64_t now_unix_s) {
    now_us_ = now_us;
    if (state_ == DtlsState::Rejected || !in_) return;
    ++stats_.datagrams_in;
    if (cfg_.trace && state_ != DtlsState::Authenticated) cfg_.trace("rx", now_us, peer_.id, describe_records(bytes));
    BIO_write(in_, bytes.data(), static_cast<int>(bytes.size()));
    drive(now_us, now_unix_s);
}

void DtlsSession::service(std::uint64_t now_us, std::uint64_t now_unix_s) {
    now_us_ = now_us;
    if (state_ == DtlsState::Rejected || !ssl_) return;
    if (!started_) {
        started_ = true;
        started_us_ = now_us;
        if (cfg_.invitation.expires_unix_s <= now_unix_s) return reject("invitation_expired");
    }
    if (state_ == DtlsState::Handshaking) {
        timeval tv{};
        if (DTLSv1_get_timeout(ssl_, &tv) == 1 && tv.tv_sec == 0 && tv.tv_usec == 0) {
            if (DTLSv1_handle_timeout(ssl_) > 0) {
                ++stats_.timeouts_handled;  // retransmit the last flight
                if (cfg_.trace) cfg_.trace("timer", now_us, peer_.id, "");
            }
        }
        if (now_us - started_us_ > cfg_.handshake_timeout_us) return reject("handshake_timeout");
    }
    if (state_ == DtlsState::Binding) {
        if (now_us - binding_started_us_ > cfg_.binding_timeout_us) return reject("binding_timeout");
        if (cfg_.send_binding && now_us - last_binding_us_ >= cfg_.binding_resend_us) send_binding(now_us);
        if (cfg_.confirm_binding && peer_bound_ && now_us - peer_bound_us_ >= cfg_.confirm_timeout_us) maybe_authenticate(true);
    }
    drive(now_us, now_unix_s);
}

void DtlsSession::drive(std::uint64_t now_us, std::uint64_t now_unix_s) {
    ERR_clear_error();
    if (state_ == DtlsState::Handshaking) {
        const int rc = SSL_do_handshake(ssl_);
        if (rc == 1) {
            // Fresh per handshake (ECDHE): the confirmation both peers must agree on for THIS invitation.
            const auto ctx = encode_invitation(cfg_.invitation);
            confirmation_.assign(kConfirmationSize, 0);
            if (SSL_export_keying_material(ssl_, confirmation_.data(), confirmation_.size(), kBindingLabel,
                                           std::strlen(kBindingLabel), ctx.data(), ctx.size(), 1) != 1) {
                flush_out();
                return reject("exporter_failed");
            }
            state_ = DtlsState::Binding;
            binding_started_us_ = now_us;
            if (cfg_.trace) cfg_.trace("handshake_done", now_us, peer_.id, "");
            if (cfg_.send_binding) send_binding(now_us);
        } else {
            const int e = SSL_get_error(ssl_, rc);
            if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
                const long v = SSL_get_verify_result(ssl_);
                flush_out();  // let the alert go out
                return reject(std::string("handshake_failed: ") + (v != X509_V_OK ? X509_verify_cert_error_string(v) : "") +
                              " " + last_errors());
            }
        }
    }
    if (state_ == DtlsState::Binding || state_ == DtlsState::Authenticated) read_records(now_us, now_unix_s);
    flush_out();
}

void DtlsSession::read_records(std::uint64_t now_us, std::uint64_t now_unix_s) {
    std::array<std::uint8_t, 2048> buf{};
    for (;;) {
        const int n = SSL_read(ssl_, buf.data(), static_cast<int>(buf.size()));
        if (n <= 0) {
            const int e = SSL_get_error(ssl_, n);
            if (e == SSL_ERROR_ZERO_RETURN) reject("closed_by_peer");
            else if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) reject("record_error: " + last_errors());
            return;
        }
        const std::span<const std::uint8_t> rec(buf.data(), static_cast<std::size_t>(n));
        if (rec[0] == kTypeBinding) {
            // Without confirm_binding, "bound" and Authenticated coincide, so this is the original condition.
            const bool was_bound = peer_bound_;
            on_binding(rec.subspan(1), now_unix_s);
            if (state_ == DtlsState::Rejected) return;
            // A binding arriving after we are done means the peer has not seen ours yet (it resends until it
            // has): answer it. Not when this binding is the one that completed us, or both sides would echo.
            if (was_bound && cfg_.send_binding) {
                const bool reply = now_us - last_binding_us_ >= cfg_.binding_resend_us;
                if (cfg_.trace) cfg_.trace(reply ? "binding_reply" : "binding_reply_suppressed", now_us, peer_.id, "");
                if (reply) send_binding(now_us);
                else ++stats_.binding_replies_suppressed;  // bounded: never more than the peer's own resend rate
            }
        } else if (rec[0] == kTypeApp) {
            if (state_ != DtlsState::Authenticated) {
                ++stats_.app_dropped_before_auth;  // fail closed: nothing before the binding is verified
                if (cfg_.trace) cfg_.trace("app_dropped_before_auth", now_us, peer_.id, "");  // KI-35 / EXP-042
                continue;
            }
            ++stats_.app_in;
            received_.emplace_back(rec.begin() + 1, rec.end());
        } else if (rec[0] == kTypeBindingAck && cfg_.confirm_binding) {
            // Only meaningful inside this authenticated DTLS session (a forger has no keys; replays are dropped by
            // DTLS). It never authenticates by itself: the peer's binding must still be verified here.
            ++stats_.binding_acks_received;
            if (cfg_.trace && state_ != DtlsState::Authenticated) cfg_.trace("binding_ack_rx", now_us, peer_.id, "");
            peer_acked_ = true;
            maybe_authenticate(false);
        }
    }
}

void DtlsSession::on_binding(std::span<const std::uint8_t> msg, std::uint64_t now_unix_s) {
    ++stats_.binding_received;
    std::size_t used = 0;
    const auto theirs = decode_invitation(msg, used);
    if (!theirs || msg.size() != used + kConfirmationSize) return reject("binding_malformed");
    if (theirs->id != cfg_.invitation.id) return reject("binding_invitation_mismatch");
    if (theirs->protocol_version != cfg_.invitation.protocol_version) return reject("binding_version_mismatch");
    if (theirs->expires_unix_s != cfg_.invitation.expires_unix_s) return reject("binding_expiry_mismatch");
    if (cfg_.invitation.expires_unix_s <= now_unix_s) return reject("invitation_expired");
    // Both sides derived the value over THEIR invitation; equal only if the invitations and keys agree.
    if (CRYPTO_memcmp(msg.data() + used, confirmation_.data(), kConfirmationSize) != 0) return reject("binding_confirmation_mismatch");
    if (!peer_bound_) {
        peer_bound_ = true;
        peer_bound_us_ = now_us_;
        if (cfg_.confirm_binding && cfg_.send_binding) send_binding_ack();
    }
    maybe_authenticate(false);
}

void DtlsSession::maybe_authenticate(bool gate_expired) {
    if (state_ != DtlsState::Binding || !peer_bound_) return;
    if (cfg_.confirm_binding && !peer_acked_) {
        if (!gate_expired) return;
        ++stats_.gate_timeouts;  // a peer without the experiment (or a lost ack streak): proceed as without it
        if (cfg_.trace) cfg_.trace("gate_timeout", now_us_, peer_.id, "");
    }
    state_ = DtlsState::Authenticated;
    if (cfg_.trace) cfg_.trace("authenticated", now_us_, peer_.id, "");
}

void DtlsSession::send_binding_ack() {
    const std::uint8_t m[1] = {kTypeBindingAck};
    if (SSL_write(ssl_, m, 1) > 0) ++stats_.binding_acks_sent;
    flush_out();
}

void DtlsSession::send_binding(std::uint64_t now_us) {
    std::vector<std::uint8_t> m{kTypeBinding};
    const auto inv = encode_invitation(cfg_.invitation);
    m.insert(m.end(), inv.begin(), inv.end());
    m.insert(m.end(), confirmation_.begin(), confirmation_.end());
    if (cfg_.corrupt_confirmation) m.back() ^= 0x01;
    if (SSL_write(ssl_, m.data(), static_cast<int>(m.size())) > 0) ++stats_.binding_sent;
    last_binding_us_ = now_us;
    flush_out();
    if (cfg_.confirm_binding && peer_bound_) send_binding_ack();  // every resend repeats the ack: it may be lost
}

SendResult DtlsSession::send(std::span<const std::uint8_t> bytes, std::uint64_t now_us) {
    if (now_us != 0) now_us_ = now_us;
    if (state_ != DtlsState::Authenticated) return SendResult::Failed;
    std::vector<std::uint8_t> m{kTypeApp};
    m.insert(m.end(), bytes.begin(), bytes.end());
    ERR_clear_error();
    if (SSL_write(ssl_, m.data(), static_cast<int>(m.size())) <= 0) return SendResult::Failed;
    ++stats_.app_out;
    return flush_out();
}

std::vector<std::vector<std::uint8_t>> DtlsSession::take_received() { return std::exchange(received_, {}); }

std::string DtlsSession::peer_fingerprint() const {
    if (!ssl_) return {};
    EVP_PKEY* k = SSL_get0_peer_rpk(ssl_);
    return k ? spki_fingerprint(spki_der(k)) : std::string();
}

}  // namespace mordor::net
