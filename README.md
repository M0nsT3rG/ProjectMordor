# Project Mordor

A research project into whether two legitimate owners of *Middle-earth: Shadow of War* (Steam, Windows x64) could one day play the campaign together in co-op.

**Status: Phase 1 (research and feasibility) is complete.** The result is a conditional NO-GO for game integration and a GO for lawful groundwork. Read [docs/phase1-report.md](docs/phase1-report.md).

> This repository contains **no game code, no game assets and no game integration**. The networking prototype is standalone and has never been connected to the game. Nothing here claims co-op in Shadow of War works.

## Progress

*Last updated 2026-10-25.*

**Overall: about 16% (a rough estimate).**

```
[███░░░░░░░░░░░░░░░░░]  ~16%
```

- **This is not a forecast that co-op will work.** The [feasibility matrix](docs/feasibility-matrix.md) deliberately gives no odds of success. Every game-side milestone is unverified, and none may start before gate G0.
- **Everything done so far is groundwork that does not touch the game.** In-game co-op is at 0%.

| Area | Weight | Done | What it means |
|---|---|---|---|
| Phase 1: research and feasibility | 5% | **100%** | Complete: inventory, prior art, evidence system, risks, report ([phase1-report.md](docs/phase1-report.md)) |
| Networking groundwork (standalone, no game) | 15% | **~60%** | Done: host and client; two transports (raw UDP and Valve GameNetworkingSockets); encrypted and authenticated sessions (DTLS); reconnect; reliable game events; real-time soak tests with fixed pass/fail gates; immutable test evidence. Still open: tests over a real LAN and the internet, the slow-recovery tail on GNS under heavy loss (KI-35), an independent security review, NAT traversal, delta snapshots and desync digests. |
| Gate G0: legal position (D-007) and target platform (D-008) | 5% | **50%** | **D-007 decided** (2026-10-25): the owner proceeds with interoperability work and accepts the legal risk; no protection is ever bypassed. **D-008 pending:** the Steam build is protected, so it cannot be the target; GOG lists the game as DRM-free (a store claim, to be verified on a purchased copy). G0 stays closed until D-008 is decided. |
| In-game milestones M1–M10 (observe the game, sync the player, puppet character, movement, combat, AI, Nemesis, missions, saves, full campaign) | 75% | **0%** | Blocked by G0. See the [matrix](docs/feasibility-matrix.md). |

**How the number is computed:**

- 5% × 100% + 15% × 60% + 5% × 50% + 75% × 0% = 16.5%. The "60%" for the networking work is itself a judgement, so read this as "between 12% and 18%".
- The weights say where most of the unknown work lies: inside the game. They are an estimate, not a measurement.

**Where the newer work is:**

- Everything after Phase 1 is in open pull requests, stacked one on another (#1 → #22), each reviewed. **None is merged into `main` yet**, so `main` still shows the Phase 1 code. The [pull requests](../../pulls) and their review comments have the details.
- The live status and test results are on the newest branch, in `.ai-collaboration/PROJECT_STATUS.md` and `TEST_RESULTS.md`.

## Contents

| Path | What it is |
|---|---|
| `docs/` | Report, architecture notes, feasibility matrix, risk register, decisions, protocol |
| `evidence/` | Findings (`F-*`) and experiments (`EXP-*`) as JSON, validated by `tools/evidence/evidence.py` |
| `tools/environment/` | Read-only game discovery and PE inventory (Python stdlib only) |
| `src/` | C++20 networking prototype: `common`, `network`, `simulation`, `host`, `client` |
| `tests/` | Unit, integration (loopback UDP) and network-impairment test suites |
| `scripts/` | `build.ps1`, `test.ps1`, `inventory.ps1` |

## Quick start

Requirements: Windows 10/11, Visual Studio with the MSVC C++ toolset, CMake 3.25 or later, Python 3.10 or later.

```powershell
./scripts/build.ps1
./scripts/test.ps1 -E2E
./scripts/inventory.ps1   # optional: needs the game installed via Steam; strictly read-only
```

## Rules

The project's non-negotiable rules are in [CLAUDE.md](CLAUDE.md). The most important ones:

- never modify the game or saves;
- never bypass DRM, anti-tamper or licensing;
- never touch official online services;
- never redistribute game files;
- never invent evidence.
