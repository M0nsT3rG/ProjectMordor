// EXP-037 probe (KI-35, review #25): per-connection phases of a secure join under the harsh profile. A secure Host and
// Client in one process, real time (1 ms loop, 1 ms timer resolution). Each trial: a fresh Client joins (inner
// transport, DTLS with the cookie, invitation binding, session ConnectRequest/Accept), then leaves. One JSON line per
// trial on stdout; the client's own log (for its ConnectRequest counts) goes to the given file.
// tools/ki35/run_secure_probe.py runs the predeclared batches and judges them.
//   ki35_secure_probe udp|gns TRIALS SEED CLIENT_LOG
#include <openssl/evp.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include <thread>

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

std::uint64_t real_us() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Trace {
    double first_hello = -1, inner = -1, auth = -1;
    int hellos = 0, timers = 0;
    std::uint64_t loops = 0;  // connect-loop iterations: loop_ms checks the 1 ms polling actually held
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: ki35_secure_probe udp|gns TRIALS SEED CLIENT_LOG\n");
        return 2;
    }
    const TransportKind kind = std::string(argv[1]) == "gns" ? TransportKind::Gns : TransportKind::Udp;
    const int trials = std::atoi(argv[2]);
    const std::uint64_t seed = std::strtoull(argv[3], nullptr, 10);
    request_1ms_timer_resolution();
    EVP_PKEY* host_key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
    EVP_PKEY* client_key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
    if (!host_key || !client_key) return 2;
    struct GnsExit {  // KI-40: GNS shut down after every transport is gone, on every exit path
        bool done = false;
        ~GnsExit() { if (!done) gns_shutdown_for_exit(); }
    } gns_exit;
    const Invitation inv{"exp037", kProtocolVersion, static_cast<std::uint64_t>(std::time(nullptr)) + 3600};
    ImpairmentConfig harsh;  // scripts/soak.ps1 profile 'harsh'
    harsh.latency_ms = 80;
    harsh.jitter_ms = 40;
    harsh.loss = 0.15;
    harsh.duplicate = 0.05;
    harsh.seed = seed;
    {
        host::HostConfig hc;
        hc.transport = kind;
        hc.impairment = harsh;
        auto hs = std::make_shared<SecureTransportConfig>();
        hs->own_key = host_key;
        hs->pinned_peers = {spki_der(client_key)};
        hs->invitation = inv;
        hc.secure = hs;
        Logger host_log("host", nullptr);
        host::Host h(hc, host_log);
        std::string err;
        if (!h.start(&err)) {
            std::fprintf(stderr, "host: %s\n", err.c_str());
            return 2;
        }
        std::FILE* clog = cli::open_log(argv[4]);
        if (!clog) {  // without it every session_requests would read as 0 (Codex, PR #25)
            std::fprintf(stderr, "client log: cannot open\n");
            return 2;
        }
        const auto log_start = real_us();  // the client log's t_ms counts from here: maps each trial onto it
        Logger client_log("client", clog);
        for (int i = 0; i < trials; ++i) {
            Trace tr;
            const auto t0 = real_us();
            auto cs = std::make_shared<SecureTransportConfig>();
            cs->own_key = client_key;
            cs->pinned_peers = {spki_der(host_key)};
            cs->invitation = inv;
            cs->trace = [&tr, t0](const char* ev, std::uint64_t now, std::uint32_t, std::string_view detail) {
                const double ms = static_cast<double>(now - t0) / 1000.0;
                const std::string_view e(ev);
                if (e == "tx" && (detail == "22/0/1" || detail.starts_with("22/0/1,"))) {  // not 22/0/11 or 22/0/16
                    ++tr.hellos;
                    if (tr.first_hello < 0) tr.first_hello = ms;
                } else if (e == "timer") {
                    if (tr.auth < 0) ++tr.timers;
                } else if (e == "inner_connected" && tr.inner < 0) {
                    tr.inner = ms;
                } else if (e == "authenticated" && tr.auth < 0) {
                    tr.auth = ms;
                }
            };
            client::ClientConfig cc;
            cc.host = Endpoint::loopback(h.port());
            cc.transport = kind;
            ImpairmentConfig ci = harsh;
            ci.seed = seed * 1000 + static_cast<std::uint64_t>(i);  // UDP: per-trial client stream; GNS ignores seeds
            cc.impairment = ci;
            cc.secure = cs;
            double connected = -1, loop_ms = -1;
            {
                client::Client c(cc, client_log);
                if (!c.start(&err)) {
                    std::fprintf(stderr, "client: %s\n", err.c_str());
                    return 2;
                }
                c.connect(real_us());
                while (real_us() - t0 < 20'000'000) {
                    const auto now = real_us();
                    h.update(now);
                    c.update(now);
                    ++tr.loops;
                    if (c.state() == client::ClientState::Connected) {
                        connected = static_cast<double>(now - t0) / 1000.0;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                loop_ms = static_cast<double>(real_us() - t0) / 1000.0 / static_cast<double>(tr.loops);
                if (connected >= 0) c.leave(real_us());
                const auto t1 = real_us();
                while (c.leaving() && real_us() - t1 < 2'000'000) {
                    const auto now = real_us();
                    h.update(now);
                    c.update(now);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            const auto t2 = real_us();
            while (real_us() - t2 < 100'000) {  // the host settles the departure before the next join
                h.update(real_us());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::printf("{\"trial\":%d,\"log_ms\":%.1f,\"ok\":%s,\"inner\":%.1f,\"first_hello\":%.1f,\"auth\":%.1f,\"connected\":%.1f,"
                        "\"hellos\":%d,\"timers\":%d,\"loop_ms\":%.2f}\n",
                        i, static_cast<double>(t0 - log_start) / 1000.0, connected >= 0 ? "true" : "false", tr.inner, tr.first_hello, tr.auth, connected, tr.hellos, tr.timers,
                        loop_ms);
            std::fflush(stdout);
        }
        h.shutdown();
        if (clog) std::fclose(clog);
    }
    EVP_PKEY_free(host_key);
    EVP_PKEY_free(client_key);
    gns_exit.done = true;
    return gns_shutdown_for_exit() ? 0 : 3;  // KI-40: last
}
