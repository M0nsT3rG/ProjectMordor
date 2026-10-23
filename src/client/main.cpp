// mordor-client: connects to a host and drives its entity with a scripted bot pattern,
// logging a status line every second (state, RTT, positions, correction stats).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "client/client.hpp"
#include "common/cli.hpp"
#include "common/precise_sleep.hpp"

namespace {
std::atomic<bool> g_stop{false};
BOOL WINAPI on_ctrl(DWORD) {
    g_stop = true;
    return TRUE;
}
std::uint64_t now_us() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

int main(int argc, char** argv) {
    using namespace mordor;
    try {
        cli::Args args(argc, argv);
        if (args.has("help")) {
            std::cout << "mordor-client [options]\n"
                         "  --host ADDR       host IPv4 address (default 127.0.0.1)\n"
                         "  --port N          host UDP port (default 47800)\n"
                         "  --pattern P       bot movement: circle | line | idle (default circle)\n"
                         "  --duration-s N    disconnect after N seconds (default 10)\n"
                         "  --no-prediction   disable client-side prediction\n"
                         "  --stall-at-s N    freeze the client loop at N seconds (simulates a paused process)\n"
                         "  --stall-s N       stall length in seconds (default 5)\n"
                      << cli::kImpairmentHelp;
            return 0;
        }
        std::FILE* sink = stderr;
        if (args.has("log") && !(sink = cli::open_log(args.str("log", "")))) {
            std::cerr << "cannot open log file\n";
            return 2;
        }
        Logger log("client", sink);
        PreciseSleeper sleeper;
        log.info("loop_timer", {kv("high_resolution", sleeper.high_resolution())});
        client::ClientConfig cfg;
        auto host = net::Endpoint::parse(args.str("host", "127.0.0.1"), args.num<std::uint16_t>("port", 47800));
        if (!host) throw std::invalid_argument("invalid --host address");
        cfg.host = *host;
        cfg.prediction = !args.has("no-prediction");
        cfg.impairment = cli::impairment_from(args);
        const std::string pattern = args.str("pattern", "circle");
        const double duration_s = args.num<double>("duration-s", 10.0);
        const double stall_at_s = args.num<double>("stall-at-s", -1.0);
        const double stall_s = args.num<double>("stall-s", 5.0);
        bool stalled = false;

        client::Client c(cfg, log);
        std::string err;
        if (!c.start(&err)) {
            log.error("start_failed", {kv("error", err)});
            return 1;
        }
        SetConsoleCtrlHandler(on_ctrl, TRUE);
        const auto start = now_us();
        c.connect(start);
        auto next_status = start + 1'000'000;
        while (!g_stop) {
            const auto t = now_us();
            const double el = (t - start) / 1e6;
            if (el >= duration_s) break;
            if (!stalled && stall_at_s >= 0 && el >= stall_at_s) {
                // Nothing is sent, received or simulated while frozen, like a suspended process.
                stalled = true;
                log.info("stall_begin", {kv("stall_ms", static_cast<std::uint64_t>(stall_s * 1000))});
                std::this_thread::sleep_for(std::chrono::duration<double>(stall_s));
                log.info("stall_end");
                continue;
            }
            if (pattern == "circle") c.set_move(static_cast<float>(std::cos(el)), static_cast<float>(std::sin(el)));
            else if (pattern == "line") c.set_move(std::fmod(el, 4.0) < 2.0 ? 1.0f : -1.0f, 0.0f);
            else c.set_move(0, 0);
            c.update(t);
            const auto st = c.state();
            if (st == client::ClientState::Rejected || st == client::ClientState::Failed ||
                st == client::ClientState::Disconnected)
                break;
            if (t >= next_status) {
                next_status += 1'000'000;
                const auto p = c.predicted_self();
                const auto& s = c.stats();
                log.info("status", {kv("state", client::to_string(st)), kv("player", c.player_id()),
                                    kv("rtt_ms", c.rtt_ms().value_or(-1.0)), kv("x", p ? p->x : 0.0f), kv("z", p ? p->z : 0.0f),
                                    kv("remotes", c.remote_entities().size()), kv("snapshots", s.snapshots_applied),
                                    kv("stale", s.stale_snapshots), kv("corrections", s.corrections),
                                    kv("max_correction", s.max_correction), kv("reconnects", s.reconnects)});
            }
            sleeper.sleep_us(1000);
        }
        const auto final_state = c.state();
        {
            // Telemetry for tools/soak/analyze.py. Percentiles use nearest-rank on raw heartbeat RTT samples.
            auto rtt = c.rtt_samples_ms();
            auto corr = c.correction_samples();
            std::sort(rtt.begin(), rtt.end());
            std::sort(corr.begin(), corr.end());
            auto pct_of = [](const std::vector<float>& v, double p, double empty) -> double {
                if (v.empty()) return empty;
                const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(v.size())));
                return v[std::clamp<std::size_t>(rank, 1, v.size()) - 1];
            };
            auto pct = [&](double p) { return pct_of(rtt, p, -1.0); };
            const auto& s = c.stats();
            log.info("summary", {kv("final_state", client::to_string(final_state)), kv("player", c.player_id()),
                                 kv("rtt_samples", rtt.size()), kv("rtt_p50_ms", pct(50)), kv("rtt_p95_ms", pct(95)),
                                 kv("rtt_p99_ms", pct(99)), kv("rtt_max_ms", rtt.empty() ? -1.0 : static_cast<double>(rtt.back())),
                                 kv("snapshots", s.snapshots_applied), kv("stale", s.stale_snapshots),
                                 kv("corrections", s.corrections), kv("max_correction", s.max_correction),
                                 kv("correction_p50", pct_of(corr, 50, 0.0)), kv("correction_p99", pct_of(corr, 99, 0.0)),
                                 kv("stale_notices", s.stale_notices),
                                 kv("reconnects", s.reconnects), kv("max_reconnect_ms", s.max_reconnect_ms),
                                 kv("invalid", s.invalid_packets), kv("bytes_in", s.bytes_in), kv("bytes_out", s.bytes_out),
                                 kv("packets_in", s.packets_in), kv("packets_out", s.packets_out),
                                 kv("elapsed_ms", (now_us() - start) / 1000)});
        }
        c.disconnect();
        c.close();
        if (sink != stderr) std::fclose(sink);
        return final_state == client::ClientState::Connected ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
