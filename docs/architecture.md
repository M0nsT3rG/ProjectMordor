# Architecture: what is known about Shadow of War, and what is not

**Status (end of Phase 1):** nothing below about game internals is CONFIRMED for our build, except facts read directly from file headers and config files (F-ENV-*). Everything else comes from third parties and is classified PARTIALLY_CONFIRMED or HYPOTHESIS. Subsystem detail is in [subsystems/](subsystems/).

## 1. What we verified ourselves

| Fact | Evidence |
|---|---|
| Exact build: 1.0.8874.0 / buildid 3095001 / SHA-256 `ED0D95CA…0BDAEF` | F-ENV-001 |
| Wwise and Scaleform statically linked (exported symbols) | F-ENV-005 |
| HTTP stack (`wininet`, `winhttp`) and `steam_api64` imported; no static `ws2_32` | F-ENV-006 |
| Protected-looking layout: entry point in `.bind`, odd section table, tiny import table, no ASLR | F-ENV-003, F-ENV-004 (HYPOTHESIS about *which* protector) |
| LithTech-style config key `GameClientShell.*` | F-ENV-007 |
| No mod loader installed | F-ENV-002 |

## 2. Third-party picture (unverified)

```
              ShadowOfWar.exe  (Firebird / "Kraken", LithTech lineage)   F-COMM-007, F-COOP-011, F-SOWM-008
  ┌──────────────────────────────────────────────────────────────────────────────────────┐
  │ ClientShell / GameClientShell ──► UI: Scaleform GFx front end      F-SOWM-008, F-ENV-005│
  │   │                               Audio: Wwise                     F-ENV-005           │
  │   ├─ input: Win32 Raw Input (WM_INPUT)                            F-SOWM-012           │
  │   ├─ camera: transform update reachable from a GameClient object  F-COMM-019           │
  │   ├─ "engine singleton" -> player -> position floats              F-SOWM-010 (low)     │
  │   ├─ time-scale requests (pause)                                  F-SOWM-014 (low)     │
  │   └─ world load / front-end transitions                           F-SOWM-006/007       │
  │ Server-side logic (in-process): client/server split in source paths, NetMesh host-vote │
  │   state machine present but runtime use unknown                  F-COOP-011           │
  │ Nemesis = "Faction": single thread, central event handler, one global LCG   F-COOP-012, F-COMM-011 │
  │ Data: *.arch06 (LTAR, Oodle) -> gamedb (GADB), SKEL, MMSH, SCRI ...  F-COMM-002..005, F-SOWM-016 │
  │ Saves: <uid>_ShadowOfWar.sav, magic SOM3, AES-CBC              F-COMM-010              │
  │ Online: WB backend, async modes only                          F-COMM-012, F-COMM-013   │
  │ Protection: SteamStub + Denuvo (Steam build)                  F-SOWM-004, F-COMM-014/017│
  └──────────────────────────────────────────────────────────────────────────────────────┘
```

## 3. Architecture questions from the Phase 1 brief

| Area | Question | Current answer | Class |
|---|---|---|---|
| A. Game loop | How do simulation updates occur? Are there identifiable subsystem boundaries? | Unknown. A per-frame cursor function and a world-load hook are claimed (F-SOWM-006/007). A simulation time-scale manager is claimed (F-SOWM-014). There is no evidence of a fixed-tick simulation separate from rendering. | HYPOTHESIS |
| B. Entity management | Actor registration, ids, components, lifecycle | No public mapping. The "engine singleton → player" pointer chain is explicitly marked unverified by its author (F-SOWM-010). | UNKNOWN |
| C. Player representation | Is character logic tied to one protagonist? | Not answered by evidence. Third-party designs assume one local player object reached through a singleton (F-SOWM-010), which hints at a single-player assumption. | HYPOTHESIS |
| D. Input | How does input reach the character controller? | Raw Input via the window procedure (F-SOWM-012). The path from there to the controller is unknown. | PARTIALLY_CONFIRMED (input source only) |
| E. Animation | Animation states vs gameplay events | Only the file-level SKEL format is partly mapped (F-COMM-005). No runtime knowledge. | UNKNOWN |
| F. Combat | Central or distributed damage resolution? | No public mapping (F-SOWM-013). Only a `Combat.Action.InventoryItem…` naming hint (F-SOWM-011). | UNKNOWN |
| G. AI | What must stay authoritative? | Design answer (ours): AI decisions, ownership and all random rolls on the host. One owner per actor with an epoch counter (F-COOP-003). The engine side is unknown. | Design only |
| H. Missions | Single-player assumptions | Unknown. Effect capture is the proposed approach (F-COOP-004). | UNKNOWN |
| I. Nemesis | Persistence and event relationships | Claimed: a central faction event handler on one thread plus a global LCG (F-COOP-012). Persistence is in the encrypted save (F-COMM-010). A WB patent covers the design (F-COMM-016). | HYPOTHESIS |

## 4. Target co-op architecture (design intent, not implemented in the game)

```
 Host PC                                                  Client PC
 ┌───────────────────────┐   inputs (UDP, redundant)   ┌───────────────────────┐
 │ Game (authoritative)  │ ◄────────────────────────── │ Game (presentation)   │
 │  + host bridge        │ ──────────────────────────► │  + client bridge      │
 │  owns AI, combat,     │   snapshots / results /     │  applies results,     │
 │  Nemesis, missions,   │   effect lists              │  puppets host player, │
 │  canonical save       │                             │  predicts own avatar  │
 └───────────────────────┘                             └───────────────────────┘
```

- The independent prototype in `src/` implements the transport, session and replication core of this picture with placeholder entities.
- The "bridge" boxes need runtime integration with the game. Phase 1 found **no lawful path** to build them under the project rules for the Steam build (see [feasibility-matrix.md](feasibility-matrix.md) and [decisions.md](decisions.md) D-003).

## 5. Biggest unknowns

1. Whether any runtime observation of the Steam build is possible without circumventing SteamStub or Denuvo, and without breaching the EULA. This is legal plus technical, and it **gates everything**.
2. Whether the engine can host a second controllable humanoid with independent input. Nobody has shown this publicly.
3. Whether Nemesis and mission scripts can be driven or observed as discrete events (effect capture) rather than opaque state.
4. Whether the in-engine client/server split and NetMesh code (F-COOP-011) are live or dead code. If live, it may be the most natural integration seam.
