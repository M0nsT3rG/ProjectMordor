#pragma once
// Minimal dependency-free test harness plus helpers for driving host/client with a simulated clock.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "client/client.hpp"
#include "common/precise_sleep.hpp"
#include "host/host.hpp"

namespace mordor::test {

// Backend under test: MORDOR_TRANSPORT=udp (default) or gns. Results must be recorded per transport.
inline net::TransportKind test_transport() {
    static const net::TransportKind kind = [] {
        char* v = nullptr;
        std::size_t n = 0;
        std::string s = (_dupenv_s(&v, &n, "MORDOR_TRANSPORT") == 0 && v) ? v : "udp";
        std::free(v);
        const auto k = net::parse_transport(s);
        if (!k || !net::transport_available(*k)) {
            std::fprintf(stderr, "MORDOR_TRANSPORT=%s is not available in this build\n", s.c_str());
            std::exit(2);
        }
        return *k;
    }();
    return kind;
}

// A skipped test is reported as SKIPPED with its reason, never as PASSED.
struct SkipTest : std::runtime_error {
    using std::runtime_error::runtime_error;
};
#define UDP_ONLY(reason)                                                                     \
    do {                                                                                     \
        if (::mordor::test::test_transport() != ::mordor::net::TransportKind::Udp)           \
            throw ::mordor::test::SkipTest(std::string("UDP-only: ") + (reason));            \
    } while (0)

// One simulated step. With GNS the library runs on real time (handshakes, keepalives, retransmits), so the
// simulated clock is coupled to real time by sleeping each step; with UDP a yield is enough.
inline void step_wait(std::uint64_t step_us) {
    if (test_transport() == net::TransportKind::Udp) {
        std::this_thread::yield();
        return;
    }
    thread_local PreciseSleeper sleeper;
    sleeper.sleep_us(static_cast<std::uint32_t>(step_us));
}

struct TestCase {
    const char* name;
    void (*fn)();
};
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Registrar {
    Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};
struct CheckFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define TEST(name)                                              \
    static void name();                                         \
    static ::mordor::test::Registrar reg_##name(#name, &name);  \
    static void name()

#define CHECK(cond)                                                                                            \
    do {                                                                                                       \
        if (!(cond))                                                                                           \
            throw ::mordor::test::CheckFailure(std::string(__FILE__) + ":" + std::to_string(__LINE__) +       \
                                               ": CHECK(" #cond ")");                                          \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                                  \
    do {                                                                                                       \
        const double va_ = (a), vb_ = (b);                                                                     \
        if (!(std::fabs(va_ - vb_) <= (eps)))                                                                  \
            throw ::mordor::test::CheckFailure(std::string(__FILE__) + ":" + std::to_string(__LINE__) +       \
                                               ": CHECK_NEAR(" #a ", " #b ") " + std::to_string(va_) + " vs " + \
                                               std::to_string(vb_));                                           \
    } while (0)

// Logs are silent unless MORDOR_TEST_LOG=1.
inline Logger& test_logger(const char* comp) {
    static std::vector<std::unique_ptr<Logger>> loggers;
    char* v = nullptr;
    std::size_t n = 0;
    const bool on = _dupenv_s(&v, &n, "MORDOR_TEST_LOG") == 0 && v && std::string(v) == "1";
    std::free(v);
    loggers.push_back(std::make_unique<Logger>(comp, on ? stderr : nullptr, LogLevel::Debug));
    return *loggers.back();
}

struct SimClock {
    std::uint64_t now = 1'000'000;
};

// Steps the simulated clock and every participant until done() or the simulated deadline.
// Real loopback delivery is asynchronous, so each step yields the thread once.
inline bool pump(SimClock& clk, host::Host* h, std::vector<client::Client*> clients, const std::function<bool()>& done,
                 std::uint64_t max_sim_us, std::uint64_t step_us = 5'000) {
    const auto deadline = clk.now + max_sim_us;
    while (clk.now < deadline) {
        if (h) h->update(clk.now);
        for (auto* c : clients) c->update(clk.now);
        if (done && done()) return true;
        clk.now += step_us;
        step_wait(step_us);
    }
    return done ? done() : true;
}

inline void run_for(SimClock& clk, host::Host* h, std::vector<client::Client*> clients, std::uint64_t sim_us) {
    pump(clk, h, std::move(clients), nullptr, sim_us);
}

struct Session {
    SimClock clk;
    host::Host host;
    std::vector<std::unique_ptr<client::Client>> clients;

    static host::HostConfig with_transport(host::HostConfig hc) {
        hc.transport = test_transport();
        return hc;
    }
    explicit Session(host::HostConfig hc = {}) : host(with_transport(hc), test_logger("host")) {
        std::string err;
        if (!host.start(&err)) throw CheckFailure("host start failed: " + err);
    }
    client::Client& add_client(client::ClientConfig cc = {}) {
        cc.host = net::Endpoint::loopback(host.port());
        cc.transport = test_transport();
        clients.push_back(std::make_unique<client::Client>(cc, test_logger("client")));
        std::string err;
        if (!clients.back()->start(&err)) throw CheckFailure("client start failed: " + err);
        clients.back()->connect(clk.now);
        return *clients.back();
    }
    std::vector<client::Client*> ptrs() {
        std::vector<client::Client*> v;
        for (auto& c : clients) v.push_back(c.get());
        return v;
    }
    bool pump_until(const std::function<bool()>& done, std::uint64_t max_sim_us) {
        return pump(clk, &host, ptrs(), done, max_sim_us);
    }
    void run(std::uint64_t sim_us) { run_for(clk, &host, ptrs(), sim_us); }
    bool all_connected() {
        for (auto& c : clients)
            if (c->state() != client::ClientState::Connected) return false;
        return true;
    }
};

}  // namespace mordor::test
