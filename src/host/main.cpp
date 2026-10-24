// mordor-host: runs the authoritative placeholder world until Ctrl+C or --duration-s elapses.
#include <ctime>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "common/cli.hpp"
#include "common/precise_sleep.hpp"
#include "host/host.hpp"

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
            std::cout << "mordor-host [options]\n"
                         "  --bind ADDR       IPv4 address to bind (default 127.0.0.1; a LAN address needs firewall approval)\n"
                         "  --port N          UDP port (default 47800)\n"
                         "  --tick-rate N     simulation ticks per second (default 30)\n"
                         "  --duration-s N    stop after N seconds (default: run until Ctrl+C)\n"
                         "  --stop-file PATH  stop cleanly once PATH exists (checked every 100 ms)\n"
                         "  --event-interval-ms N  send every player a reliable game event this often (0 = off); players in\n"
                         "                    their grace period get them queued and replayed on reconnect (FB-9-02)\n"
                      << cli::kImpairmentHelp << cli::kSecureHelp;
            return 0;
        }
        std::FILE* sink = stderr;
        if (args.has("log") && !(sink = cli::open_log(args.str("log", "")))) {
            std::cerr << "cannot open log file\n";
            return 2;
        }
        Logger log("host", sink);
        log.info("clock", {kv("mono_us", now_us())});  // maps this log's t (ms since start) onto the shared steady clock
        PreciseSleeper sleeper;
        log.info("loop_timer", {kv("high_resolution", sleeper.high_resolution())});
        host::HostConfig cfg;
        auto bind = net::Endpoint::parse(args.str("bind", "127.0.0.1"), args.num<std::uint16_t>("port", 47800));
        if (!bind) throw std::invalid_argument("invalid --bind address");
        cfg.bind = *bind;
        cfg.tick_rate_hz = args.num<std::uint16_t>("tick-rate", 30);
        cfg.impairment = cli::impairment_from(args); cfg.transport = cli::transport_from(args);
        // Secure mode: kept alive for the whole run (it owns the identity key). Never logs key material.
        const auto secure = cli::secure_from(args, true, static_cast<std::uint64_t>(std::time(nullptr)));
        if (secure.cfg)  // KI-35 handshake timeline: record types and milestones only, never key material
            secure.cfg->trace = [&log](const char* ev, std::uint64_t t_us, std::uint32_t peer, std::string_view detail) {
                log.info("dtls_trace", {kv("ev", ev), kv("mono_us", t_us), kv("peer", peer), kv("detail", detail)});
            };
        cfg.secure = secure.cfg;
        log.info("security", {kv("mode", secure.cfg ? "dtls" : "plaintext"), kv("own_fingerprint", secure.own_fingerprint),
                              kv("invitation", secure.invitation_id)});
        const double duration_s = args.num<double>("duration-s", 0.0);
        // FB-14-02: a test harness can end the run once every participant is done, instead of guessing a duration.
        const std::string stop_file = args.str("stop-file", "");
        const auto event_interval_us = args.num<std::uint64_t>("event-interval-ms", 0) * 1000;
        std::uint64_t next_event_us = 0, events_refused = 0;

        host::Host h(cfg, log);
        std::string err;
        if (!h.start(&err)) {
            log.error("start_failed", {kv("error", err)});
            return 1;
        }
        SetConsoleCtrlHandler(on_ctrl, TRUE);
        const auto start = now_us();
        std::uint64_t next_stop_check_us = 0;
        while (!g_stop) {
            const auto t = now_us();
            if (duration_s > 0 && (t - start) / 1e6 >= duration_s) break;
            if (!stop_file.empty() && t >= next_stop_check_us) {
                next_stop_check_us = t + 100'000;
                std::error_code ec;
                if (std::filesystem::exists(stop_file, ec)) {
                    log.info("stop_file_seen", {kv("uptime_ms", (t - start) / 1000)});
                    break;
                }
            }
            h.update(t);
            if (event_interval_us && t >= next_event_us) {
                next_event_us = t + event_interval_us;
                for (auto id : h.player_ids()) {
                    const std::uint32_t n = static_cast<std::uint32_t>(h.stats().events_queued + 1);
                    const std::uint8_t payload[4] = {static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(n >> 8),
                                                     static_cast<std::uint8_t>(n >> 16), static_cast<std::uint8_t>(n >> 24)};
                    if (!h.send_event(id, 1, payload, t)) ++events_refused;
                }
            }
            sleeper.sleep_us(1000);
        }
        if (event_interval_us) log.info("event_source", {kv("events_refused", events_refused)});
        h.shutdown();
        if (sink != stderr) std::fclose(sink);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
