# Project Mordor

A research project into whether two legitimate owners of *Middle-earth: Shadow of War* (Steam, Windows x64) could one day play the campaign together in co-op.

**Status: Phase 1 (research and feasibility) is complete.** The result is a conditional NO-GO for game integration and a GO for lawful groundwork. Read [docs/phase1-report.md](docs/phase1-report.md).

> This repository contains **no game code, no game assets and no game integration**. The networking prototype is standalone and has never been connected to the game. Nothing here claims co-op in Shadow of War works.

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
