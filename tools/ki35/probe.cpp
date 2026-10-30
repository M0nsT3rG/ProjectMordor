// EXP-036 probe (KI-35): times a GNS client's connect() until its Connected event, through our own GnsTransport, under
// the harsh profile (applied by our set_impairment, process-global, so both directions are impaired as in the soak) and
// unimpaired. One listening transport; a fresh client transport per trial; both polled every 1 ms at 1 ms timer
// resolution. Prints one JSON object (samples in ms) on stdout; tools/ki35/run_probe.py records and judges it.
//   ki35_probe [harsh_trials] [clean_trials]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "timer_resolution.hpp"

#include "network/gns_transport.hpp"
#include "network/transport.hpp"

using namespace mordor::net;

namespace {

std::uint64_t now_us() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Series {
    std::vector<double> ms;
    int failed = 0;
};

Series run(const ImpairmentConfig& imp, int trials) {
    Series s;
    std::string err;
    auto host = make_transport(TransportKind::Gns, {}, &err);
    if (!host || !host->open(Endpoint::loopback(0), true, &err) || !host->set_impairment(imp)) {
        std::fprintf(stderr, "host: %s\n", err.c_str());
        std::exit(2);
    }
    const auto to = Endpoint::loopback(host->local_endpoint().port);
    std::vector<TransportEvent> ev;
    for (int i = 0; i < trials; ++i) {
        auto c = make_transport(TransportKind::Gns, {}, &err);
        if (!c || !c->open(Endpoint::loopback(0), false, &err) || !c->set_impairment(imp)) {
            std::fprintf(stderr, "client: %s\n", err.c_str());
            std::exit(2);
        }
        const auto t0 = now_us();
        const auto peer = c->connect(to);
        bool up = false;
        while (peer && !up && now_us() - t0 < 20'000'000) {
            ev.clear();
            host->poll(now_us(), ev);
            ev.clear();
            c->poll(now_us(), ev);
            for (const auto& e : ev) up = up || (e.kind == TransportEvent::Kind::Connected && e.peer == *peer);
            if (!up) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (up) s.ms.push_back(static_cast<double>(now_us() - t0) / 1000.0);
        else ++s.failed;
        if (peer) c->close_peer(*peer);
        for (int k = 0; k < 30; ++k) {  // let both sides see the close before the next trial
            ev.clear();
            host->poll(now_us(), ev);
            ev.clear();
            c->poll(now_us(), ev);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        c->close();
    }
    host->close();
    return s;
}

void print(const char* name, const Series& s, bool last) {
    std::printf("\"%s\":{\"failed\":%d,\"ms\":[", name, s.failed);
    for (std::size_t i = 0; i < s.ms.size(); ++i) std::printf("%s%.1f", i ? "," : "", s.ms[i]);
    std::printf("]}%s", last ? "" : ",");
}

}  // namespace

int main(int argc, char** argv) {
    const int harsh_n = argc > 1 ? std::atoi(argv[1]) : 300;
    const int clean_n = argc > 2 ? std::atoi(argv[2]) : 50;
    request_1ms_timer_resolution();
    ImpairmentConfig harsh;  // scripts/soak.ps1 profile 'harsh'
    harsh.latency_ms = 80;
    harsh.jitter_ms = 40;
    harsh.loss = 0.15;
    harsh.duplicate = 0.05;
    const Series h = run(harsh, harsh_n);
    const Series c = run(ImpairmentConfig{}, clean_n);
    std::printf("{\"impairment\":{\"latency_ms\":80,\"jitter_ms\":40,\"loss\":0.15,\"duplicate\":0.05},");
    print("harsh", h, false);
    print("clean", c, true);
    std::printf("}\n");
    const bool down = gns_shutdown_for_exit();  // KI-40: last, after every GNS transport is closed
    return down ? 0 : 3;
}
