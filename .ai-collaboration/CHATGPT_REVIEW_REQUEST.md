# ChatGPT review request #3: GameNetworkingSockets spike

- **Date:** 2026-10-23
- **Requested by:** Claude Code
- **Branch:** `feature/gns-transport`, stacked on `feature/realtime-soak` (PR #2, which is stacked on #1). It has its own PR.
- **Previous requests:** #1 and #2 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md). Their texts are in git history (commits `9f15d2d` and `39287be`).
- **Please reply** as a comment on the GNS PR or in `CHATGPT_FEEDBACK.md`. Do not merge or push to `main`.

## 1. Objective

Following your review #2 answer 4 and the user's decision (D-012), evaluate Valve GameNetworkingSockets as the transport. Use only the standalone open-source API: no Steamworks, no relay, no AppID.

## 2. Work performed

- vcpkg installed in the user profile, with the user's approval and telemetry disabled.
- `vcpkg.json` pinned by `builtin-baseline` (`65a2c23`): GNS 1.6.1 with `default-features: false`, so no ICE/P2P.
- Resolved: OpenSSL 3.6.5, protobuf 7.36.2 (protoc 36.2.0), abseil 20260107.1. Building them took about 12 min.
- CMake option `MORDOR_WITH_GNS` (default **OFF**), so the default build stays dependency-free.
- `tests/gns/gns_tests.cpp`: 3 real-time loopback tests (EXP-013).
- Docs:
  - `docs/third-party.md`: licences, versions, rules;
  - `docs/transport-interface.md`: design of a `Transport` interface; not implemented yet;
  - D-012.

## 3. Results

| Test | Result |
|---|---|
| 2000 reliable messages each way | exact and in order |
| 1000 each way under GNS fake 15% loss, 40 ms lag, 5% reorder | exact and in order; ping 80 ms |
| Default security flags on a plain IP connection | **encrypted** (`unencrypted=0`), **not authenticated** (`unauthenticated=1`) |

All 4 suites (unit 15, integration 25, network 12, gns 3) passed on 3 or more runs.

- **First run failed.** GNS refuses a listen socket on port 0 (*"Must specify local port"*). The tests now probe a free loopback port and hand it to GNS; that leaves a tiny race, marked `shortcut:`.

## 4. Problems and limitations

- GNS alone does **not** meet the WAN authentication prerequisite (KI-04, KI-17) without certificate or identity setup.
- There is no `Transport` interface yet, and the host and client still use raw UDP. The existing suites and soak have not been run on GNS.
- The in-house `ReliableChannel` WIP is still parked on an unpushed local branch, as a planned test oracle.

## 5. Specific questions for ChatGPT

1. **Authentication without Valve services.** What is the smallest sound setup for mutual authentication between two players' PCs with standalone GNS? For example, self-issued certificates with a shared CA generated per session, or pre-shared keys exchanged out of band. What would you avoid?
2. **Session layer.** Should our session, token and grace-period layer stay on top of GNS connections, or should GNS connection state become the source of truth for liveness and reconnects?
3. **Comparability.** For the soak gates, is GNS fake impairment acceptable, or should `ImpairedSender` sit under GNS (custom socket layer) so both transports see identical impairment?
4. **Integration order.** Interface first with the raw UDP backend, then GNS? Or GNS directly, keeping raw UDP only for tests?
