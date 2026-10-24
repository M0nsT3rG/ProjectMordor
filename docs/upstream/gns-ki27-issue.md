# DRAFT: upstream issue for ValveSoftware/GameNetworkingSockets (KI-27)

> **Status: draft for the owner's review. NOT published.** Posting it publicly needs the owner's approval (project rule: outward-facing actions are confirmed first). Nothing below contains user data, paths or game information.

---

**Title:** `Assertion Failed: pSock->m_callback.m_fnCallback` in `PollRawUDPSockets` when a listen socket is closed right after `GameNetworkingSockets_Init`

**Version:** 1.6.1 (vcpkg port `gamenetworkingsockets`, default features off), Windows 11 x64, MSVC 14.51, Release build. `DBGFLAG_ASSERT` is defined in release builds (`tier0/dbg.h`), so the assert is reported through the debug output callback.

**Summary.** If a listen socket is created and closed immediately after `GameNetworkingSockets_Init`, the service thread sometimes reports:

```
steamnetworkingsockets_socketthread.cpp(2609): Assertion Failed: pSock->m_callback.m_fnCallback
```

Waiting 10 ms or more after Init before closing the socket made it disappear in our runs.

**Minimal reproducer** (public API only):

```cpp
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>
#include <atomic>
#include <cstdio>
#include <cstring>
static std::atomic<int> g_asserts{0};
static void on_debug(ESteamNetworkingSocketsDebugOutputType, const char* msg) {
    if (std::strstr(msg, "Assertion Failed")) ++g_asserts;
}
int main() {
    for (int i = 0; i < 400; ++i) {
        SteamDatagramErrMsg err{};
        if (!GameNetworkingSockets_Init(nullptr, err)) return 2;
        SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Warning, on_debug);
        SteamNetworkingIPAddr addr;
        addr.Clear();
        addr.SetIPv4(0x7f000001, 47900);
        HSteamListenSocket s = SteamNetworkingSockets()->CreateListenSocketIP(addr, 0, nullptr);
        if (s != k_HSteamListenSocket_Invalid) SteamNetworkingSockets()->CloseListenSocket(s);
        GameNetworkingSockets_Kill();
    }
    std::printf("assertions: %d\n", g_asserts.load());
}
```

**Observed** (9 runs of 400 iterations each):

| Wait after Init | Assertions |
|---|---|
| 0 ms | 11, 18, 21, 44, 35, 34 (163 / 2400 iterations) |
| 10 ms | 0, 0, 0 |
| 50 ms | 0, 0, 0, 0, 0, 0 |

**Reading of the source (our interpretation; please correct us):**
- `CRawUDPSocketImpl::Close()` → `InternalAddToCleanupQueue()` sets `m_callback.m_fnCallback = nullptr`. The comment there says this "marks the socket as pending destruction".
- If the service thread is running, the deletion is deferred to `ProcessPendingDestroyClosedRawUDPSockets()`.
- `PollRawUDPSockets()` starts with a `DBGFLAG_ASSERT` sanity loop, `Assert( pSock->m_callback.m_fnCallback )`, over all of `s_vecRawSockets`. This loop includes sockets that are legitimately pending destruction.
- It looks like a socket closed while the service thread is still starting up can be seen by that first sanity check before any cleanup pass has run.

If that is right, the assert is a false positive. Possible fixes:
- skip sockets with a null callback in the sanity loop (they are pending destruction by design); or
- call `ProcessPendingDestroyClosedRawUDPSockets()` before the check.

**Impact for us:** none functionally observed (tests pass). We currently work around it by initialising once per process and waiting 50 ms after the first Init.

---

## Notes for the owner (not part of the issue)

- Evidence: `evidence/logs/ki27-repro-2026-10-24.json` holds the second batch (0 ms: 44, 35, 34; 10 ms: 0, 0, 0; 50 ms: 0, 0, 0). The first batch (0 ms: 11, 18, 21; 50 ms: 0, 0, 0) was read from the console only and has no log file; `evidence/logs/ki27-probe-2026-10-24.json`, EXP-018 and EXP-019. Reproducer source: `tools/ki27-repro/repro.cpp` (target `mordor_ki27_repro`, GNS build).
- Before posting, check whether the upstream `master` already changed `PollRawUDPSockets`. I have **not** checked the current upstream source; this analysis is against the 1.6.1 tarball that vcpkg used.
- To post it, open a GitHub issue on `ValveSoftware/GameNetworkingSockets` from your own account.
