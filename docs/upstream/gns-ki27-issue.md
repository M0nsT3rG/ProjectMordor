# DRAFT: upstream issue for ValveSoftware/GameNetworkingSockets (KI-27)

> **Status: POSTED 2026-10-24** as https://github.com/ValveSoftware/GameNetworkingSockets/issues/458. Draft v2 was approved by the owner and posted by Claude Code from the owner's account at the owner's explicit request. The posted text is the content between the two horizontal rules below, without the **Title:** line, which became the issue title. Everything between the two horizontal rules below is the proposed issue text, in English; the owner notes follow it.

---

**Title:** `Assertion Failed: pSock->m_callback.m_fnCallback` (socketthread.cpp:2609) after re-initializing the library with `GameNetworkingSockets_Kill` + `GameNetworkingSockets_Init`

### Summary

In a process that has already called `GameNetworkingSockets_Init` and `GameNetworkingSockets_Kill` once, a second `Init` followed by creating and immediately closing a listen socket sometimes makes the service thread report:

```
src/steamnetworkingsockets/clientlib/steamnetworkingsockets_socketthread.cpp(2609): Assertion Failed: pSock->m_callback.m_fnCallback
```

- **Not observed after the first `Init`:** 0 of 500 processes that `Init` once.
- **Observed after a re-`Init`:** 18 of 200 processes that `Init` twice.
- **Not observed when the code waits 10 ms after `Init`** before creating the socket.

No crash or functional failure was observed; the report comes from the `DBGFLAG_ASSERT` sanity check, which `tier0/dbg.h` compiles into release builds by default.

### Versions

- GameNetworkingSockets **v1.6.1** (tag commit `d534c19aa760df3fb75fd20db13ba1932b8a5463`). On 2026-10-24 this is also the head of `master`, and `steamnetworkingsockets_socketthread.cpp` at `master` is byte-identical to the v1.6.1 copy we built.
- The library was built with vcpkg (commit `65a2c2391af39b773f83f91e0ab6c8ced8f9c727`), port `gamenetworkingsockets` 1.6.1 with `default-features: false`, triplet `x64-windows` (OpenSSL 3.6.5, protobuf 7.36.2).
- Windows 11 Pro x64 (build 26300), Visual Studio 2026 (MSVC 19.51.36260), CMake 4.4.4, Release configuration.

### Reproducer (public API only)

Three files in an empty directory.

`main.cpp`:

```cpp
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
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.21)
project(gns_listen_close_repro CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(GameNetworkingSockets CONFIG REQUIRED)
add_executable(gns_listen_close_repro main.cpp)
target_link_libraries(gns_listen_close_repro PRIVATE GameNetworkingSockets::shared)
```

`vcpkg.json`:

```json
{
  "name": "gns-listen-close-repro",
  "version-string": "1.0.0",
  "builtin-baseline": "65a2c2391af39b773f83f91e0ab6c8ced8f9c727",
  "dependencies": [
    { "name": "gamenetworkingsockets", "default-features": false }
  ]
}
```

### Build and run on Windows

From a "Developer PowerShell for VS" (or any shell with CMake and a VS 2022/2026 toolchain), with vcpkg cloned to `$env:USERPROFILE\vcpkg` and bootstrapped:

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:USERPROFILE\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
.\build\Release\gns_listen_close_repro.exe 400 0     # 400 Init/Kill cycles, no wait after Init
.\build\Release\gns_listen_close_repro.exe 400 10    # the same with a 10 ms wait after Init
.\build\Release\gns_listen_close_repro.exe 1 0       # first Init only (run many times, one process each)
.\build\Release\gns_listen_close_repro.exe 2 0       # one re-Init (run many times, one process each)
```

The first configure builds OpenSSL, protobuf and abseil through vcpkg, which can take a while; later builds reuse the vcpkg binary cache. The program only binds `127.0.0.1:47900` (third argument changes the port).

**Expected:** `"assertions": 0` in every run.
**Actual:** non-zero counts in the runs without a wait (see Results).

### Results

| Scenario | Processes | Assertions | Processes with ≥1 assertion |
|---|---|---|---|
| 1 iteration per process (first `Init` only), no wait | 300 | 0 | 0 |
| 2 iterations per process (one re-`Init`), no wait | 200 | 18 | 18 |
| 400 iterations, no wait | 3 | 116 (39, 38, 39) | 3 |
| 400 iterations, 10 ms wait after `Init` | 3 | 0 (0, 0, 0) | 0 |

**Recorded runs.** The rows above come from one batch whose per-process JSON lines were written to a file as they ran. An earlier batch, also recorded to a file, gave the same picture:
- 400 iterations with no wait: 44, 35 and 34 assertions;
- 10 ms wait: 0, 0, 0;
- 50 ms wait: 0, 0, 0.

Both batches used an earlier build of this program. It runs the same loop; only the name of one output field (`settle_ms` instead of `wait_ms`) and the formatting of the assert text differ.

**Console-only observations.** These were not written to a file:
- 400 iterations with no wait: 11, 18 and 21 assertions, plus 40 with the exact `main.cpp` above in a fresh build;
- 10 ms wait with the exact `main.cpp`: 0;
- first `Init` only: 0 assertions in another 500 processes;
- one re-`Init`: 25 of another 200 processes asserted.

### What we looked at in the source (a hypothesis, not a confirmed cause)

- **The check that fires.** `PollRawUDPSockets()` begins with a `DBGFLAG_ASSERT` loop, `Assert( pSock->m_callback.m_fnCallback )`, over every entry in `s_vecRawSockets`.
- **What makes the callback null.** `CRawUDPSocketImpl::Close()` → `InternalAddToCleanupQueue()` sets `m_callback.m_fnCallback = nullptr`. The comment there calls this the marker for a socket "pending destruction". When the service thread is running, the actual deletion is deferred to `ProcessPendingDestroyClosedRawUDPSockets()`, which runs from `ProcessDeferredOperations()` after polling.
- **Hypothesis.** A socket that is legitimately pending destruction can be seen by that sanity loop before the deferred cleanup runs. The fact that it only happens after a re-`Init` suggests the window depends on state or timing carried over from the previous library lifetime: for example how the newly started service thread interleaves with the first close, or a static that is not reset by `Kill`.
- **Similar, but not the same assert.** #230 (closed) reported static state that `Kill` did not reset (`s_bRecreatePollList`, POSIX).

We have not confirmed any of this with a debugger, a stack trace, or instrumentation of the library, so please treat it as a hypothesis. If the analysis is right, skipping sockets with a null callback in that sanity loop (they are pending destruction by design), or processing pending destructions before the check, would avoid the false positive. Happy to test a patch.

### Impact

No crash or misbehaviour observed; our application tests pass. We currently avoid it by initializing the library once per process (no `Kill`/re-`Init` cycles) and waiting 50 ms after `Init`. That workaround is empirical.

### Duplicate search

Searched open and closed issues and PRs on 2026-10-24 for: `m_fnCallback`, `m_callback.m_fnCallback`, `PollRawUDPSockets`, `ProcessPendingDestroyClosedRawUDPSockets`, `socketthread.cpp Assertion`, `CloseListenSocket assert`, `pending destruction socket assert`, `Assertion Failed Kill Init`, `2609`, `Assertion Failed`. No issue reports this assert. #230 is related (re-initialization state), but it is a different assert and platform.

---

## Notes for the owner (not part of the issue)

- **Checks done for this version:**
  1. **Upstream source:** `master` = `v1.6.1` (`d534c19`). The file is byte-identical to the one vcpkg built (`diff` exit 0); the assert is at line 2609 in both.
  2. **Duplicates:** searched as listed; none found; #230 cited as related.
  3. **The cause is labelled a hypothesis** in the text, with what was not done (no debugger, no stack trace).
  4. **Build instructions:** verified by building `tools/ki27-repro` from an empty directory exactly as written. The vcpkg dependencies were restored from this machine's binary cache, and that run gave 40 assertions with no wait and 0 with 10 ms (console). A machine without that cache builds OpenSSL, protobuf and abseil first.
  5. **Recorded vs console-only:** separated in Results.
     - Recorded: `evidence/logs/ki27-repro-per-process-2026-10-24.jsonl` (506 lines, one per process; the table) and `evidence/logs/ki27-repro-2026-10-24.json` (44/35/34; 10 ms 0/0/0; 50 ms 0/0/0).
     - Both came from the earlier build of the reproducer (`tools/ki27-repro/repro.cpp`, removed from the tree but kept in git history at `6551ab0`, same loop).
     - The exact `main.cpp` in the issue has been run only interactively: 40 with no wait, 0 with 10 ms. **I can produce a recorded batch with exactly that `main.cpp` before posting, if you prefer.**
  6. **Private information:** the three files contain no project names, paths, user names, e-mail, keys or game references (grep audit). The issue text mentions only public versions, the loopback address and port 47900. Paths appear only as `$env:USERPROFILE` placeholders.
- **What changed since draft v1:**
  - the finding that only a re-`Init` triggers it, which corrects v1's "startup" framing;
  - versions and the master comparison;
  - the full build;
  - the duplicate search;
  - the recorded/console split.
- **How to post (only after your confirmation):** open a new issue at `https://github.com/ValveSoftware/GameNetworkingSockets/issues/new` from your own account, and paste the text between the rules. I will not post it.
