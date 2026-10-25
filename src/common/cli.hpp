#pragma once
// Tiny "--key value" / "--flag" argument parser shared by the host and client executables.
#include <share.h>

#include <charconv>
#include <cstdio>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "network/impairment.hpp"
#include "network/secure_transport.hpp"
#include "network/transport.hpp"
#ifdef MORDOR_HAVE_DTLS
#include "network/dtls_session.hpp"
#include "network/secure_identity.hpp"
#endif

namespace mordor::cli {

class Args {
public:
    Args(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            std::string_view a = argv[i];
            if (!a.starts_with("--")) throw std::invalid_argument("unexpected argument: " + std::string(a));
            a.remove_prefix(2);
            if (i + 1 < argc && !std::string_view(argv[i + 1]).starts_with("--"))
                values_[std::string(a)] = argv[++i];
            else
                values_[std::string(a)] = "";
        }
    }
    bool has(const std::string& k) const { return values_.contains(k); }
    std::string str(const std::string& k, std::string def) const {
        auto it = values_.find(k);
        return it == values_.end() ? def : it->second;
    }
    template <class T>
    T num(const std::string& k, T def) const {
        auto it = values_.find(k);
        if (it == values_.end()) return def;
        T v{};
        const auto& s = it->second;
        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (ec != std::errc{} || p != s.data() + s.size()) throw std::invalid_argument("bad number for --" + k + ": " + s);
        return v;
    }

private:
    std::map<std::string, std::string> values_;
};

// fopen_s opens files exclusively, so tooling (test.ps1, log tails) could not read a live log and a
// concurrent reader made the open fail. Deny writers only.
inline std::FILE* open_log(const std::string& path) { return _fsopen(path.c_str(), "w", _SH_DENYWR); }

inline net::ImpairmentConfig impairment_from(const Args& a) {
    net::ImpairmentConfig c;
    c.loss = a.num<double>("loss", 0.0);
    c.duplicate = a.num<double>("duplicate", 0.0);
    c.latency_ms = a.num<std::uint32_t>("latency-ms", 0);
    c.jitter_ms = a.num<std::uint32_t>("jitter-ms", 0);
    c.seed = a.num<std::uint64_t>("seed", 1);
    if (!c.valid()) throw std::invalid_argument("--loss and --duplicate must be within [0,1]");
    return c;
}

inline net::TransportKind transport_from(const Args& a) {
    const auto s = a.str("transport", "udp");
    const auto k = net::parse_transport(s);
    if (!k) throw std::invalid_argument("--transport must be udp or gns, got: " + s);
    if (!net::transport_available(*k)) throw std::invalid_argument("--transport " + s + " is not available in this build");
    return *k;
}

// --connect-deadline-s: overall bound for one connect attempt (D-014 amended). Out of range is an error, not a clamp.
inline std::uint64_t connect_deadline_us_from(const Args& a, std::uint64_t min_us, std::uint64_t max_us, std::uint64_t def_us) {
    const double s = a.num<double>("connect-deadline-s", static_cast<double>(def_us) / 1e6);
    const auto us = static_cast<std::uint64_t>(s * 1e6);
    if (!(s > 0) || us < min_us || us > max_us)
        throw std::invalid_argument("--connect-deadline-s must be within [" + std::to_string(min_us / 1'000'000) + ", " +
                                    std::to_string(max_us / 1'000'000) + "] seconds");
    return us;
}

// Secure mode (review #8 FB-8-03). No --secure-* option = plaintext, explicitly. Any of them = ALL required ones,
// validated here; anything missing, unreadable, mismatched or expired throws, so startup fails: no fallback.
//   host:   --secure-key FILE --invitation STRING --allow-peer B64[,B64...]
//   client: --secure-key FILE --invitation STRING   (the host key comes from the invitation)
struct SecureSetup {
#ifdef MORDOR_HAVE_DTLS
    net::PKeyPtr key;  // must outlive the transport
#endif
    std::shared_ptr<net::SecureTransportConfig> cfg;  // nullptr: plaintext
    std::string own_fingerprint, invitation_id;
};
inline SecureSetup secure_from(const Args& a, bool host, std::uint64_t now_unix_s) {
    SecureSetup out;
    const bool any = a.has("secure-key") || a.has("invitation") || a.has("allow-peer");
    if (!any) return out;
#ifndef MORDOR_HAVE_DTLS
    (void)host;
    (void)now_unix_s;
    throw std::invalid_argument("secure mode needs a MORDOR_WITH_GNS build (OpenSSL); refusing to run in plaintext");
#else
    if (!a.has("secure-key") || a.str("secure-key", "").empty()) throw std::invalid_argument("secure mode: --secure-key FILE is required");
    if (!a.has("invitation") || a.str("invitation", "").empty()) throw std::invalid_argument("secure mode: --invitation is required");
    if (host != a.has("allow-peer")) throw std::invalid_argument(host ? "secure host: --allow-peer B64[,B64...] is required"
                                                                      : "secure client: --allow-peer is a host option");
    std::string err;
    out.key = net::load_identity_file(a.str("secure-key", ""), &err);
    if (!out.key) throw std::invalid_argument("secure mode: " + err);
    auto inv = net::decode_invitation(a.str("invitation", ""), &err);
    if (!inv) throw std::invalid_argument("secure mode: " + err);
    if (inv->invitation.expires_unix_s <= now_unix_s) throw std::invalid_argument("secure mode: the invitation has expired");
    if (inv->invitation.protocol_version != net::kProtocolVersion)
        throw std::invalid_argument("secure mode: the invitation is for another protocol version");
    const auto own_spki = net::spki_der(out.key.get());
    auto cfg = std::make_shared<net::SecureTransportConfig>();
    cfg->own_key = out.key.get();
    cfg->skip_cookie_when_inner_proves_routability = a.has("dtls-skip-cookie-if-routable");  // FB-15-01 experiment
    cfg->invitation = inv->invitation;
    if (host) {
        if (own_spki != inv->host_spki) throw std::invalid_argument("secure host: the invitation was made for a different host key");
        std::string list = a.str("allow-peer", "");
        for (std::size_t pos = 0; pos <= list.size();) {
            const auto end = std::min(list.find(',', pos), list.size());
            auto der = net::base64_decode(std::string_view(list).substr(pos, end - pos));
            if (!der) throw std::invalid_argument("secure host: --allow-peer entry is not a base64 public key");
            cfg->pinned_peers.push_back(std::move(*der));
            pos = end + 1;
        }
    } else {
        cfg->pinned_peers = {inv->host_spki};
    }
    out.own_fingerprint = net::spki_fingerprint(own_spki);
    out.invitation_id = inv->invitation.id;
    out.cfg = std::move(cfg);
    return out;
#endif
}

inline constexpr const char* kSecureHelp =
    "  --secure-key FILE  DPAPI-protected identity key (mordor-keygen --new); enables secure mode, no plaintext fallback\n"
    "  --invitation STR   invitation from the host (mordor-keygen --invite); public fields only\n"
    "  --allow-peer B64   host only: comma-separated public keys of the invited players\n"
    "  --dtls-skip-cookie-if-routable  host only, experiment (FB-15-01): no DTLS cookie on transports that prove\n"
    "                     return routability themselves (GNS); UDP always keeps it\n";

inline constexpr const char* kImpairmentHelp =
    "  --transport T     udp (default) or gns (needs a MORDOR_WITH_GNS build; impairment is then process-global)\n"
    "  --loss P          drop outgoing packets with probability P (0..1)\n"
    "  --duplicate P     duplicate outgoing packets with probability P\n"
    "  --latency-ms N    delay outgoing packets by N ms\n"
    "  --jitter-ms N     add uniform 0..N ms extra delay (reorders packets)\n"
    "  --seed N          impairment RNG seed (default 1)\n"
    "  --log FILE        write JSON-lines log to FILE instead of stderr\n";

}  // namespace mordor::cli
