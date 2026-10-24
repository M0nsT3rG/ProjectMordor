// Repeats GameNetworkingSockets_Init -> CreateListenSocketIP -> CloseListenSocket -> GameNetworkingSockets_Kill
// and counts "Assertion Failed" messages reported through the debug output callback.
// Usage: gns_listen_close_repro [iterations=400] [wait_ms_after_init=0] [port=47900]
// Prints one JSON line: {"iterations":..,"wait_ms":..,"listen_failures":..,"assertions":..,"first_assert":".."}
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

static std::atomic<int> g_asserts{0};
static std::mutex g_mutex;
static std::string g_first_assert;

static void on_debug(ESteamNetworkingSocketsDebugOutputType, const char* msg) {
    if (!std::strstr(msg, "Assertion Failed")) return;
    if (g_asserts++ == 0) {
        std::lock_guard<std::mutex> lock(g_mutex);
        const char* a = std::strstr(msg, "Assertion Failed");
        g_first_assert = a;
        while (!g_first_assert.empty() && (g_first_assert.back() == '\n' || g_first_assert.back() == '\r')) g_first_assert.pop_back();
    }
}

int main(int argc, char** argv) {
    const int iterations = argc > 1 ? std::atoi(argv[1]) : 400;
    const int wait_ms = argc > 2 ? std::atoi(argv[2]) : 0;
    const int port = argc > 3 ? std::atoi(argv[3]) : 47900;
    int listen_failures = 0;
    for (int i = 0; i < iterations; ++i) {
        SteamDatagramErrMsg err{};
        if (!GameNetworkingSockets_Init(nullptr, err)) {
            std::fprintf(stderr, "GameNetworkingSockets_Init failed: %s\n", err);
            return 2;
        }
        SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Warning, on_debug);
        if (wait_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
        SteamNetworkingIPAddr addr;
        addr.Clear();
        addr.SetIPv4(0x7f000001, static_cast<uint16>(port));  // 127.0.0.1
        const HSteamListenSocket s = SteamNetworkingSockets()->CreateListenSocketIP(addr, 0, nullptr);
        if (s == k_HSteamListenSocket_Invalid) ++listen_failures;
        else SteamNetworkingSockets()->CloseListenSocket(s);
        GameNetworkingSockets_Kill();
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    std::printf("{\"iterations\": %d, \"wait_ms\": %d, \"listen_failures\": %d, \"assertions\": %d, \"first_assert\": \"%s\"}\n",
                iterations, wait_ms, listen_failures, g_asserts.load(), g_first_assert.c_str());
    return 0;
}
