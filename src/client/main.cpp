// mordor-client: connects to a host and drives its entity with a scripted bot pattern,
// logging a status line every second (state, RTT, positions, correction stats).
#include <ctime>
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
#ifdef MORDOR_HAVE_GNS
#include "network/gns_transport.hpp"
#endif
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
                         "  --outage-at-s N   cut this process's outgoing traffic at N seconds (100% send loss)\n"
                         "  --outage-s N      outage length in seconds (default 5); with GNS only this process is cut\n"
                         "  --connect-deadline-s N  overall connect deadline, 1..120 s (default 20)\n"
                      << cli::kImpairmentHelp << cli::kSecureHelp;
            return 0;
        }
        std::FILE* sink = stderr;
        if (args.has("log") && !(sink = cli::open_log(args.str("log", "")))) {
            std::cerr << "cannot open log file\n";
            return 2;
        }
        Logger log("client", sink);
        log.info("clock", {kv("mono_us", now_us())});  // maps this log's t (ms since start) onto the shared steady clock
        PreciseSleeper sleeper;
        log.info("loop_timer", {kv("high_resolution", sleeper.high_resolution())});
        client::ClientConfig cfg;
        auto host = net::Endpoint::parse(args.str("host", "127.0.0.1"), args.num<std::uint16_t>("port", 47800));
        if (!host) throw std::invalid_argument("invalid --host address");
        cfg.host = *host;
        cfg.prediction = !args.has("no-prediction");
        cfg.impairment = cli::impairment_from(args); cfg.transport = cli::transport_from(args);
        // Secure mode: kept alive for the whole run (it owns the identity key). Never logs key material.
        const auto secure = cli::secure_from(args, false, static_cast<std::uint64_t>(std::time(nullptr)));
        if (secure.cfg)  // KI-35 handshake timeline: record types and milestones only, never key material
            secure.cfg->trace = [&log](const char* ev, std::uint64_t t_us, std::uint32_t peer, std::string_view detail) {
                log.info("dtls_trace", {kv("ev", ev), kv("mono_us", t_us), kv("peer", peer), kv("detail", detail)});
            };
        cfg.secure = secure.cfg;
        log.info("security", {kv("mode", secure.cfg ? "dtls" : "plaintext"), kv("own_fingerprint", secure.own_fingerprint),
                              kv("invitation", secure.invitation_id)});
        // FB-12-01 experiment variable: the session-level reconnect retry interval (default 500 ms), bounded.
        if (args.has("reconnect-retry-ms")) {
            const auto ms = args.num<std::uint64_t>("reconnect-retry-ms", 500);
            if (ms < 50 || ms > 5000) throw std::invalid_argument("--reconnect-retry-ms must be within [50, 5000]");
            cfg.reconnect_retry_us = ms * 1000;
        }
        log.info("retry_policy", {kv("reconnect_retry_ms", cfg.reconnect_retry_us / 1000), kv("connect_retry_ms", cfg.connect_retry_us / 1000)});
        cfg.connect_deadline_us = cli::connect_deadline_us_from(args, client::kMinConnectDeadlineUs, client::kMaxConnectDeadlineUs,
                                                                cfg.connect_deadline_us);
        const std::string pattern = args.str("pattern", "circle");
        const double duration_s = args.num<double>("duration-s", 10.0);
        const double stall_at_s = args.num<double>("stall-at-s", -1.0);
        const double stall_s = args.num<double>("stall-s", 5.0);
        bool stalled = false;
        // Review #6 FB-6-05: an outage of THIS process's uplink only. With GNS (process-global impairment) this is
        // how asymmetric scenarios run: separate processes, each with its own impairment.
        const double outage_at_s = args.num<double>("outage-at-s", -1.0);
        const double outage_s = args.num<double>("outage-s", 5.0);
        enum class Outage { Before, During, After } outage = Outage::Before;
        std::uint32_t last_event_id = 0;
        std::uint64_t events_applied = 0, event_order_errors = 0;

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
            if (outage_at_s >= 0 && outage == Outage::Before && el >= outage_at_s) {
                net::ImpairmentConfig cut = cfg.impairment;
                cut.loss = 1.0;
                if (!c.set_impairment(cut)) throw std::runtime_error("outage impairment refused");
                outage = Outage::During;
                log.info("outage_begin", {kv("outage_ms", static_cast<std::uint64_t>(outage_s * 1000))});
            } else if (outage == Outage::During && el >= outage_at_s + outage_s) {
                if (!c.set_impairment(cfg.impairment)) throw std::runtime_error("outage restore refused");
                outage = Outage::After;
                log.info("outage_end");
            }
            if (pattern == "circle") c.set_move(static_cast<float>(std::cos(el)), static_cast<float>(std::sin(el)));
            else if (pattern == "line") c.set_move(std::fmod(el, 4.0) < 2.0 ? 1.0f : -1.0f, 0.0f);
            else c.set_move(0, 0);
            c.update(t);
            // Application side of FB-9-02: apply each event (here: check the stream is contiguous) and only then ack.
            for (const auto& e : c.take_events()) {
                if (e.id != last_event_id + 1) ++event_order_errors;  // a gap or repeat visible to the application
                last_event_id = e.id;
                ++events_applied;
                c.ack_events_applied(e.id, t);
            }
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
                                    kv("max_correction", s.max_correction), kv("reconnects", s.reconnects),
                                    kv("bytes_in", s.bytes_in), kv("bytes_out", s.bytes_out)});  // window rates (v3)
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
                                 kv("rtt_samples", rtt.size()), kv("rtt_p5_ms", pct(5)), kv("rtt_p50_ms", pct(50)), kv("rtt_p95_ms", pct(95)),
                                 kv("rtt_p99_ms", pct(99)), kv("rtt_max_ms", rtt.empty() ? -1.0 : static_cast<double>(rtt.back())),
                                 kv("rtt_min_ms", rtt.empty() ? -1.0 : static_cast<double>(rtt.front())),
                                 kv("events_applied", events_applied), kv("event_order_errors", event_order_errors),
                                 kv("events_duplicate", s.events_duplicate), kv("events_out_of_order", s.events_out_of_order),
                                 kv("snapshots", s.snapshots_applied), kv("stale", s.stale_snapshots),
                                 kv("corrections", s.corrections), kv("max_correction", s.max_correction),
                                 kv("correction_p50", pct_of(corr, 50, 0.0)), kv("correction_p99", pct_of(corr, 99, 0.0)),
                                 kv("stale_notices", s.stale_notices),
                                 kv("reconnects", s.reconnects), kv("max_reconnect_ms", s.max_reconnect_ms),
                                 kv("invalid", s.invalid_packets), kv("bytes_in", s.bytes_in), kv("bytes_out", s.bytes_out),
                                 kv("packets_in", s.packets_in), kv("packets_out", s.packets_out),
                                 kv("elapsed_ms", (now_us() - start) / 1000)});
        }
        if (const auto* ts = c.transport_stats())  // realized impairment per direction (KI-35, FB-10-03)
            log.info("transport_stats", {kv("sent", ts->sent), kv("received", ts->received),
                                         kv("simulated_drops", ts->simulated_drops), kv("send_failed", ts->send_failed),
                                         kv("not_connected", ts->not_connected), kv("backpressure", ts->backpressure)});
        if (const auto* ss = c.transport_stats_secure())  // secure mode diagnostics (no secrets)
            log.info("secure_stats", {kv("handshakes_done", ss->handshakes_done), kv("handshake_retransmits", ss->handshake_retransmits),
                                      kv("handshake_ms_max", ss->handshake_ms_max), kv("handshake_ms_total", ss->handshake_ms_total),
                                      kv("pending_failed", ss->pending_failed), kv("reliable_retransmits", ss->reliable_retransmits),
                                      kv("idle_timeouts", ss->idle_timeouts)});
        // Explicit leave (FB-10-04, KI-37): wait, bounded by leave_timeout_us, for the host's DisconnectAck.
        if (final_state == client::ClientState::Connected) {
            const auto send_errors_before = c.stats().send_errors;
            const auto leave_deadline = now_us() + 3'000'000;  // never trust update() alone to end it
            c.leave(now_us());
            while (c.leaving() && now_us() < leave_deadline) {
                c.update(now_us());
                sleeper.sleep_us(1000);
            }
            const auto& s = c.stats();
            log.info("leave", {kv("leave_confirmed", s.leave_confirmed), kv("leave_ms", s.leave_ms),
                               kv("disconnects_sent", s.leave_disconnects_sent),
                               kv("disconnects_refused", s.leave_disconnects_refused),
                               kv("send_errors", s.send_errors - send_errors_before)});
        }
        c.disconnect();
        c.close();
#ifdef MORDOR_HAVE_GNS
        log.info("gns_shutdown", {kv("done", net::gns_shutdown_for_exit())});  // KI-40, see mordor-host
#endif
        if (sink != stderr) std::fclose(sink);
        return final_state == client::ClientState::Connected ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
