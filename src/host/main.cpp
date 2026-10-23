// mordor-host: runs the authoritative placeholder world until Ctrl+C or --duration-s elapses.
#include <atomic>
#include <chrono>
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

#include "common/cli.hpp"
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
                      << cli::kImpairmentHelp;
            return 0;
        }
        std::FILE* sink = stderr;
        if (args.has("log") && !(sink = cli::open_log(args.str("log", "")))) {
            std::cerr << "cannot open log file\n";
            return 2;
        }
        Logger log("host", sink);
        host::HostConfig cfg;
        auto bind = net::Endpoint::parse(args.str("bind", "127.0.0.1"), args.num<std::uint16_t>("port", 47800));
        if (!bind) throw std::invalid_argument("invalid --bind address");
        cfg.bind = *bind;
        cfg.tick_rate_hz = args.num<std::uint16_t>("tick-rate", 30);
        cfg.impairment = cli::impairment_from(args);
        const double duration_s = args.num<double>("duration-s", 0.0);

        host::Host h(cfg, log);
        std::string err;
        if (!h.start(&err)) {
            log.error("start_failed", {kv("error", err)});
            return 1;
        }
        SetConsoleCtrlHandler(on_ctrl, TRUE);
        const auto start = now_us();
        while (!g_stop) {
            const auto t = now_us();
            if (duration_s > 0 && (t - start) / 1e6 >= duration_s) break;
            h.update(t);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        h.shutdown();
        if (sink != stderr) std::fclose(sink);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
