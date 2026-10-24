// Shared runner: `<suite> [name-filter] [--json results.json]`. Exit code 1 if any test fails.
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>

#include "test_support.hpp"

#ifdef _WIN32
// KI-40 (review #12 FB-12-02): with MORDOR_LIFECYCLE_LOG=<file>, each run appends JSON lines at its exit phases
// (main returning, atexit) with what the GNS runtime still holds, so a hang at exit shows where it stopped.
// read_gns=false at atexit: the GNS runtime state is a function-local static created after our atexit registration,
// so it is already destroyed when the handler runs; reading it there would be undefined behaviour.
void lifecycle(const char* phase, bool read_gns = true);
#endif
#ifdef _WIN32
// The suites step with sleep_for(1 ms). Without a 1 ms timer resolution that sleep takes ~15.6 ms, and timing tests
// then passed or failed depending on whether some other program happened to hold 1 ms (KI-39, EXP-023). Defined at
// the end of this file, to keep <windows.h> away from the suites' headers.
void request_1ms_timer_resolution();
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    request_1ms_timer_resolution();
    lifecycle("main_started");
    std::atexit([] { lifecycle("atexit", false); });
#endif
    std::string filter, json_path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--json" && i + 1 < argc) json_path = argv[++i];
        else filter = a;
    }
    int passed = 0, failed = 0, skipped = 0;
    std::string json = "[";
    for (const auto& t : mordor::test::registry()) {
        if (!filter.empty() && std::string(t.name).find(filter) == std::string::npos) continue;
        const auto start = std::chrono::steady_clock::now();
        std::string error, skip_reason;
        try {
            t.fn();
        } catch (const mordor::test::SkipTest& s) {
            skip_reason = s.what();
        } catch (const std::exception& e) {
            error = e.what();
        } catch (...) {
            error = "unknown exception";
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        const char* result = !skip_reason.empty() ? "SKIPPED" : error.empty() ? "PASSED" : "FAILED";
        (!skip_reason.empty() ? skipped : error.empty() ? passed : failed)++;
        std::cout << (!skip_reason.empty() ? "[SKIP] " : error.empty() ? "[PASS] " : "[FAIL] ") << t.name << " (" << ms << " ms)";
        if (!error.empty()) std::cout << "\n       " << error;
        if (!skip_reason.empty()) std::cout << "\n       " << skip_reason;
        std::cout << std::endl;
        std::string esc;
        for (char c : (error.empty() ? skip_reason : error)) esc += (c == '"' || c == '\\') ? std::string("\\") + c : std::string(1, c);
        json += std::string(json.size() > 1 ? "," : "") + "\n  {\"name\":\"" + t.name + "\",\"result\":\"" + result +
                "\",\"transport\":\"" + mordor::net::to_string(mordor::test::test_transport()) + "\",\"ms\":" + std::to_string(ms) +
                ",\"error\":\"" + esc + "\"}";
    }
    json += "\n]\n";
    std::cout << passed << " passed, " << failed << " failed, " << skipped << " skipped (transport "
              << mordor::net::to_string(mordor::test::test_transport()) << ")" << std::endl;
    if (!json_path.empty()) std::ofstream(json_path) << json;
#ifdef _WIN32
    lifecycle("main_returning");
#endif
    return failed ? 1 : 0;
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

void request_1ms_timer_resolution() {
    // Windows 11 ignores timeBeginPeriod for processes without a visible window (hidden, or started by ctest) unless
    // they opt out of that power throttling. Both apply to this process only and end with it.
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    s.StateMask = 0;  // 0: do NOT ignore our timer resolution request
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof s);
    timeBeginPeriod(1);
}

#include <share.h>
#ifdef MORDOR_HAVE_GNS
#include "network/gns_transport.hpp"
#endif
void lifecycle(const char* phase, bool read_gns) {
    char* path = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&path, &len, "MORDOR_LIFECYCLE_LOG") != 0 || !path) return;
    FILE* f = _fsopen(path, "a", _SH_DENYNO);
    std::free(path);
    if (!f) return;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#ifdef MORDOR_HAVE_GNS
    if (read_gns) {
        const auto l = mordor::net::gns_lifecycle();
        std::fprintf(f, "{\"pid\":%lu,\"phase\":\"%s\",\"mono_ms\":%lld,\"gns_initialized\":%d,\"gns_refs\":%d,\"gns_connections\":%zu,\"gns_listen_sockets\":%zu}\n",
                     GetCurrentProcessId(), phase, static_cast<long long>(ms), l.initialized ? 1 : 0, l.refs, l.connections, l.listen_sockets);
        std::fclose(f);
        return;
    }
#else
    (void)read_gns;
#endif
    std::fprintf(f, "{\"pid\":%lu,\"phase\":\"%s\",\"mono_ms\":%lld}\n", GetCurrentProcessId(), phase, static_cast<long long>(ms));
    std::fclose(f);
}
#endif
