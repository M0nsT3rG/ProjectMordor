// EXP-041 probe (KI-35, FB-25-05): the phases of each STALL RECONNECT of a secure client under the harsh profile. A
// secure Host and Client in one process, real time (1 ms loop, 1 ms timer resolution). Each cycle: 1 s connected, a
// 5 s stall (the client is not updated; the host and the GNS thread run, as in the soak), then both are updated until
// the client is Connected again after Reconnecting. Defaults unchanged. One JSON line per cycle on stdout; the
// client's own log (ConnectRequests and accepts, for the timestamp attribution) goes to the given file.
// tools/ki35/run_reconnect_probe.py runs the predeclared batches and judges them (docs/soak-testing.md, EXP-041).
//   ki35_reconnect_probe udp|gns CYCLES SEED CLIENT_LOG [off|on|flat|cap750|cap500] [harsh|latency|slow]
//   (EXP-043/044/047 arm, profile)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <openssl/evp.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "timer_resolution.hpp"

#include "client/client.hpp"
#include "common/cli.hpp"
#include "common/log.hpp"
#include "host/host.hpp"
#include "network/dtls_session.hpp"
#include "network/gns_transport.hpp"
#include "network/secure_transport.hpp"

using namespace mordor;
using namespace mordor::net;

namespace {

constexpr std::uint64_t kStallUs = 5'000'000;       // the soak's stall (scripts/soak.ps1 -StallS 5)
constexpr std::uint64_t kCycleLimitUs = 20'000'000;  // a reconnect not done by then is a timeout
constexpr std::uint64_t kNoReconnectUs = 10'000'000;  // still Connected and never Reconnecting by then: no_reconnect

std::uint64_t real_us() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch()).count());
}

// One reconnect, in ms from the stall end t0; -1 = not reached. Filled by the client's DTLS trace while armed.
struct Cycle {
    std::uint64_t t0 = 0;
    bool armed = false;
    double notice = -1, inner = -1, first_hello = -1, hvr = -1, cookie_hello = -1, auth = -1;
    int hellos = 0, timers = 0;
    double host_auth = -1;            // EXP-042: the host's own `authenticated` (it verified the client's binding)
    int gate_timeouts = 0;            // EXP-043: client gate timeouts (no BindingAck within confirm_timeout)
    std::vector<double> host_drops;   // EXP-042: the host's app_dropped_before_auth, one per dropped record
    int host_timers = 0;              // EXP-044: the host's DTLS retransmission timer fires
    int client_tx = 0, host_tx = 0;   // EXP-044: DTLS datagrams each side sent before it authenticated
    // EXP-047, same window per side: bytes sent and received (-1 once a traced datagram had no size) and resent
    // datagrams (a key that side already sent since the stall ended).
    long long client_tx_bytes = 0, host_tx_bytes = 0, client_rx_bytes = 0, host_rx_bytes = 0;
    int client_resent = 0, host_resent = 0;
    std::set<std::string> client_keys, host_keys;
};

// EXP-047: "22/0/1,20/0,len=213,seq=1" -> its size and resend key: the first plaintext handshake message and its
// message_seq, or else the record list.
struct Datagram {
    long long len = -1;
    std::string key;
};
Datagram parse_detail(std::string_view d) {
    Datagram out;
    std::string records, first_hs;
    long long seq = -1;
    for (std::size_t at = 0; at <= d.size();) {
        const auto end = std::min(d.find(',', at), d.size());
        const auto tok = d.substr(at, end - at);
        if (tok.starts_with("len=")) out.len = std::atoll(std::string(tok.substr(4)).c_str());
        else if (tok.starts_with("seq=")) seq = std::atoll(std::string(tok.substr(4)).c_str());
        else {
            records += (records.empty() ? "" : ",") + std::string(tok);
            if (first_hs.empty() && tok.starts_with("22/0/")) first_hs = tok;
        }
        at = end + 1;
    }
    out.key = seq >= 0 && !first_hs.empty() ? first_hs + "#" + std::to_string(seq) : records;
    return out;
}

// Adds a traced datagram's size to `bytes` (sticky -1 when a size is missing: the cycle then fails completeness).
void add_bytes(long long& bytes, const Datagram& g) {
    if (bytes < 0) return;
    bytes = g.len < 0 ? -1 : bytes + g.len;
}

double cpu_ms() {  // EXP-044: this process's user + kernel time so far
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return -1;
    auto ticks = [](const FILETIME& f) { return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return static_cast<double>(ticks(k) + ticks(u)) / 10'000.0;  // 100 ns ticks
}

bool is_client_hello(std::string_view detail) { return detail == "22/0/1" || detail.starts_with("22/0/1,"); }

}  // namespace

int main(int argc, char** argv) {
    // EXP-043/044/047: optional ARM (off: defaults | on: confirm_binding | flat, cap750, cap500: DTLS retransmission cap
    // 250 / 750 / 500 ms, on host and client) and PROFILE (harsh | latency: the same delay without loss or duplicates |
    // slow: 150 ms + U[0, 50] ms, 1% loss). Defaults keep EXP-041/042's.
    if (argc < 5 || argc > 7) {
        std::fprintf(stderr, "usage: ki35_reconnect_probe udp|gns CYCLES SEED CLIENT_LOG [off|on|flat|cap750|cap500] [harsh|latency|slow]\n");
        return 2;
    }
    const TransportKind kind = std::string(argv[1]) == "gns" ? TransportKind::Gns : TransportKind::Udp;
    const int cycles = std::atoi(argv[2]);
    const std::uint64_t seed = std::strtoull(argv[3], nullptr, 10);
    const std::string arm = argc > 5 ? argv[5] : "off", profile = argc > 6 ? argv[6] : "harsh";
    const std::uint32_t cap_us = arm == "flat" ? 250'000 : arm == "cap750" ? 750'000 : arm == "cap500" ? 500'000 : 0;
    if ((arm != "off" && arm != "on" && cap_us == 0) || (profile != "harsh" && profile != "latency" && profile != "slow")) {
        std::fprintf(stderr, "arm must be off|on|flat|cap750|cap500 and profile harsh|latency|slow\n");
        return 2;
    }
    request_1ms_timer_resolution();
    EVP_PKEY* host_key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
    EVP_PKEY* client_key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
    if (!host_key || !client_key) return 2;
    struct GnsExit {  // KI-40: GNS shut down after every transport is gone, on every exit path
        bool done = false;
        ~GnsExit() { if (!done) gns_shutdown_for_exit(); }
    } gns_exit;
    const Invitation inv{"exp041", kProtocolVersion, static_cast<std::uint64_t>(std::time(nullptr)) + 6 * 3600};
    ImpairmentConfig harsh;  // scripts/soak.ps1 profile 'harsh'
    harsh.latency_ms = profile == "slow" ? 150 : 80;  // slow (EXP-047): an RTT of 300-400 ms, above the 250 ms timer
    harsh.jitter_ms = profile == "slow" ? 50 : 40;
    harsh.loss = profile == "harsh" ? 0.15 : profile == "slow" ? 0.01 : 0.0;  // "latency": delay only (EXP-043)
    harsh.duplicate = profile == "harsh" ? 0.05 : 0.0;
    harsh.seed = seed;
    bool aborted = false;  // the cycle loop stopped early
    Cycle cur;             // filled by both sides' DTLS traces while armed
    {
        host::HostConfig hc;
        hc.transport = kind;
        hc.impairment = harsh;
        auto hs = std::make_shared<SecureTransportConfig>();
        hs->own_key = host_key;
        hs->pinned_peers = {spki_der(client_key)};
        hs->invitation = inv;
        hs->confirm_binding = arm == "on";
        if (cap_us) hs->retransmit_max_us = cap_us;  // EXP-044 flat: the timer never grows; EXP-047 cap750 / cap500
        // EXP-042: the host's side of the reconnect handshake (one client, so every host session event is its).
        hs->trace = [&cur](const char* ev, std::uint64_t now, std::uint32_t, std::string_view detail) {
            if (!cur.armed || now < cur.t0) return;
            const double ms = static_cast<double>(now - cur.t0) / 1000.0;
            const std::string_view e(ev);
            if (e == "app_dropped_before_auth") cur.host_drops.push_back(ms);
            else if (e == "authenticated" && cur.host_auth < 0) cur.host_auth = ms;
            // EXP-044 cost window: until the host authenticated this reconnect. A late duplicate ClientHello can still
            // reach the stateless listener after that and be answered with a traced HelloVerifyRequest (Codex).
            else if (e == "timer" && cur.host_auth < 0) ++cur.host_timers;
            else if (e == "tx" && cur.host_auth < 0) {  // the HelloVerifyRequest included
                ++cur.host_tx;
                const auto g = parse_detail(detail);
                add_bytes(cur.host_tx_bytes, g);
                if (!cur.host_keys.insert(g.key).second) ++cur.host_resent;
            } else if ((e == "rx" || e == "listen_rx") && cur.host_auth < 0) {
                add_bytes(cur.host_rx_bytes, parse_detail(detail));
            }
        };
        hc.secure = hs;
        Logger host_log("host", nullptr);
        host::Host h(hc, host_log);
        std::string err;
        if (!h.start(&err)) {
            std::fprintf(stderr, "host: %s\n", err.c_str());
            return 2;
        }
        std::FILE* clog = cli::open_log(argv[4]);
        if (!clog) {  // without it the session attribution has no data (EXP-037 lesson)
            std::fprintf(stderr, "client log: cannot open\n");
            return 2;
        }
        const auto log_start = real_us();  // the client log's t_ms counts from here
        Logger client_log("client", clog);
        auto cs = std::make_shared<SecureTransportConfig>();
        cs->own_key = client_key;
        cs->pinned_peers = {spki_der(host_key)};
        cs->invitation = inv;
        cs->confirm_binding = arm == "on";
        if (cap_us) cs->retransmit_max_us = cap_us;
        cs->trace = [&cur](const char* ev, std::uint64_t now, std::uint32_t, std::string_view detail) {
            if (!cur.armed || now < cur.t0) return;
            const double ms = static_cast<double>(now - cur.t0) / 1000.0;
            const std::string_view e(ev);
            // After t0 only the reconnect's new handshake traces: an authenticated session traces nothing. The first
            // ClientHello can leave in the same update that enters Reconnecting, so no notice gate here.
            if (e == "tx" && cur.auth < 0) {  // EXP-044 cost window: until the client authenticated
                ++cur.client_tx;
                const auto g = parse_detail(detail);
                add_bytes(cur.client_tx_bytes, g);
                if (!cur.client_keys.insert(g.key).second) ++cur.client_resent;
            } else if (e == "rx" && cur.auth < 0) {
                add_bytes(cur.client_rx_bytes, parse_detail(detail));
            }
            if (e == "tx" && is_client_hello(detail)) {
                ++cur.hellos;
                if (cur.first_hello < 0) cur.first_hello = ms;
                if (cur.hvr >= 0 && cur.cookie_hello < 0) cur.cookie_hello = ms;
            } else if (e == "rx" && detail.starts_with("22/0/3") && cur.hvr < 0) {
                cur.hvr = ms;  // HelloVerifyRequest: the next ClientHello carries the cookie
            } else if (e == "timer") {
                if (cur.auth < 0) ++cur.timers;
            } else if (e == "inner_connected" && cur.inner < 0) {
                cur.inner = ms;
            } else if (e == "authenticated" && cur.auth < 0) {
                cur.auth = ms;
            } else if (e == "gate_timeout") {
                ++cur.gate_timeouts;  // EXP-043: the client proceeded without the host's BindingAck
            }
        };
        client::ClientConfig cc;
        cc.host = Endpoint::loopback(h.port());
        cc.transport = kind;
        ImpairmentConfig ci = harsh;
        ci.seed = seed * 1000 + 1;  // UDP: the client's own stream; GNS ignores seeds
        cc.impairment = ci;
        cc.secure = cs;
        std::unique_ptr<client::Client> c;
        auto pump = [&](std::uint64_t us) {  // both sides, in real time
            const auto t = real_us();
            while (real_us() - t < us) {
                const auto now = real_us();
                h.update(now);
                if (c) c->update(now);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        auto join = [&]() -> bool {  // a fresh client and join; true once Connected
            c.reset();
            c = std::make_unique<client::Client>(cc, client_log);
            if (!c->start(&err)) return false;
            c->connect(real_us());
            const auto t = real_us();
            while (real_us() - t < kCycleLimitUs) {
                const auto now = real_us();
                h.update(now);
                c->update(now);
                if (c->state() == client::ClientState::Connected) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        };
        if (!join()) {
            std::fprintf(stderr, "initial join failed: %s\n", c ? c->state_reason().c_str() : "start");
            return 2;
        }
        for (int i = 0; i < cycles; ++i) {
            pump(1'000'000);  // connected play
            const auto s0 = real_us();
            while (real_us() - s0 < kStallUs) {  // the stall: only the host (and the GNS thread) runs
                h.update(real_us());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            cur = Cycle{};
            cur.t0 = real_us();
            cur.armed = true;
            double connected = -1, first_snapshot = -1;
            std::uint64_t snaps_at_connect = 0, loops = 0;
            const char* outcome = "timeout";
            while (real_us() - cur.t0 < kCycleLimitUs) {
                const auto now = real_us();
                h.update(now);
                // Before the update: a snapshot processed in the same update as the accept counts (Codex). Snapshots
                // before the accept are not applied (not Connected), so any increase over this is after it.
                const auto snaps_before = c->stats().snapshots_applied;
                c->update(now);
                ++loops;
                const double ms = static_cast<double>(now - cur.t0) / 1000.0;
                const auto st = c->state();
                if (st == client::ClientState::Reconnecting && cur.notice < 0) cur.notice = ms;
                if (st == client::ClientState::Connected && cur.notice >= 0 && connected < 0) {
                    connected = ms;
                    snaps_at_connect = snaps_before;
                }
                if (connected >= 0 && c->stats().snapshots_applied > snaps_at_connect) {
                    first_snapshot = ms;
                    outcome = "ok";
                    break;
                }
                if (st == client::ClientState::Failed || st == client::ClientState::Rejected ||
                    st == client::ClientState::Disconnected) {
                    outcome = "failed";
                    break;
                }
                if (cur.notice < 0 && now - cur.t0 > kNoReconnectUs) {
                    outcome = "no_reconnect";
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            cur.armed = false;
            if (std::string_view(outcome) == "timeout" && connected >= 0) outcome = "no_snapshot";  // reconnected, no snapshot
            const double loop_ms = static_cast<double>(real_us() - cur.t0) / 1000.0 / static_cast<double>(loops ? loops : 1);
            std::string drops;  // EXP-042: the host's app_dropped_before_auth times, ms from t0
            for (const double d : cur.host_drops) drops += (drops.empty() ? "" : ",") + std::to_string(d);
            std::printf("{\"cycle\":%d,\"log_ms\":%.1f,\"outcome\":\"%s\",\"reason\":\"%s\",\"notice\":%.1f,\"inner\":%.1f,"
                        "\"first_hello\":%.1f,\"hvr\":%.1f,\"cookie_hello\":%.1f,\"auth\":%.1f,\"connected\":%.1f,"
                        "\"first_snapshot\":%.1f,\"hellos\":%d,\"timers\":%d,\"loop_ms\":%.2f,\"host_auth\":%.1f,"
                        "\"host_drops\":[%s],\"gate_timeouts\":%d,\"arm\":\"%s\",\"profile\":\"%s\",\"host_timers\":%d,"
                        "\"client_tx\":%d,\"host_tx\":%d,\"cpu_ms\":%.1f,\"client_tx_bytes\":%lld,\"host_tx_bytes\":%lld,"
                        "\"client_rx_bytes\":%lld,\"host_rx_bytes\":%lld,\"client_resent\":%d,\"host_resent\":%d}\n",
                        i, static_cast<double>(cur.t0 - log_start) / 1000.0, outcome, c->state_reason().c_str(), cur.notice,
                        cur.inner, cur.first_hello, cur.hvr, cur.cookie_hello, cur.auth, connected, first_snapshot,
                        cur.hellos, cur.timers, loop_ms, cur.host_auth, drops.c_str(), cur.gate_timeouts, arm.c_str(),
                        profile.c_str(), cur.host_timers, cur.client_tx, cur.host_tx, cpu_ms(), cur.client_tx_bytes,
                        cur.host_tx_bytes, cur.client_rx_bytes, cur.host_rx_bytes, cur.client_resent, cur.host_resent);
            std::fflush(stdout);
            if (std::string_view(outcome) != "ok" && c->state() != client::ClientState::Connected && !join()) {
                std::fprintf(stderr, "rejoin after cycle %d failed: %s\n", i, c->state_reason().c_str());
                aborted = true;  // a truncated batch must not look complete (Codex)
                break;
            }
        }
        c.reset();
        h.shutdown();
        std::fclose(clog);
    }
    EVP_PKEY_free(host_key);
    EVP_PKEY_free(client_key);
    gns_exit.done = true;
    if (!gns_shutdown_for_exit()) return 3;  // KI-40: last
    return aborted ? 4 : 0;
}
