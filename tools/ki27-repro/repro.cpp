// Minimal reproducer for KI-27 (review #7 FB-7-03), GNS public API only, no Project Mordor code.
// Repeats: GameNetworkingSockets_Init -> CreateListenSocketIP -> CloseListenSocket -> GameNetworkingSockets_Kill
// and counts "Assertion Failed" lines that GNS reports through its debug output.
// Usage: mordor_ki27_repro [iterations=500] [settle_ms_after_init=0] [port=47900]
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

static std::atomic<int> g_asserts{0};
static std::string g_first_assert;

static void on_debug(ESteamNetworkingSocketsDebugOutputType, const char* msg) {
    if (std::strstr(msg, "Assertion Failed")) {
        if (g_asserts++ == 0) g_first_assert = msg;
    }
}

int main(int argc, char** argv) {
    const int iterations = argc > 1 ? std::atoi(argv[1]) : 500;
    const int settle_ms = argc > 2 ? std::atoi(argv[2]) : 0;
    const int port = argc > 3 ? std::atoi(argv[3]) : 47900;
    int failures = 0;
    for (int i = 0; i < iterations; ++i) {
        SteamDatagramErrMsg err{};
        if (!GameNetworkingSockets_Init(nullptr, err)) {
            std::fprintf(stderr, "init failed: %s\n", err);
            return 2;
        }
        SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Warning, on_debug);
        if (settle_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));
        SteamNetworkingIPAddr addr;
        addr.Clear();
        addr.SetIPv4(0x7f000001, static_cast<uint16>(port));
        const HSteamListenSocket s = SteamNetworkingSockets()->CreateListenSocketIP(addr, 0, nullptr);
        if (s == k_HSteamListenSocket_Invalid) ++failures;
        else SteamNetworkingSockets()->CloseListenSocket(s);
        GameNetworkingSockets_Kill();
    }
    std::printf("{\"iterations\": %d, \"settle_ms\": %d, \"listen_failures\": %d, \"assertions\": %d, \"first\": \"%s\"}\n",
                iterations, settle_ms, failures, g_asserts.load(), g_first_assert.empty() ? "" : "Assertion Failed: pSock->m_callback.m_fnCallback");
    return 0;
}
