# Feasibility matrix: milestones M1–M10

**Every milestone is currently UNVERIFIED.** No game-side experiment was run in Phase 1. We do not assign success percentages; we state evidence, blockers and go/no-go criteria.

## Gate G0: applies to every milestone

All of M1–M10 depend on interacting with the running game or analysing its code. For the Steam build, three issues stand in the way:

1. **Protection (rules 3 and 4).** The Steam executable shows a SteamStub-like layout (F-ENV-003/004). Denuvo is declared on the store page and reported by third parties (F-COMM-014, F-COMM-017, F-SOWM-004). The only public analysis workflow first removes the Steam wrapper (F-SOWM-009). Project Mordor will not do that.
2. **EULA.** It prohibits client modification, reverse engineering and third-party programs that interact with the client (F-COMM-015).
3. **Interoperability law.** EU Directive 2009/24/EC Articles 6 and 8 may allow limited interoperability work and void contrary contract terms. Whether a co-op mod qualifies is a **legal question for the user and a qualified lawyer**. This project gives no legal advice.

**G0 passes only when both of these hold:**

- The user records a decision in `docs/decisions.md` covering points 2 and 3.
- A target and method exist that require no circumvention of any protection. One candidate is a DRM-free release: third parties say the GOG version has no DRM (F-COMM-017), and both players would need such copies.
  - **2026-10-27:** the owner's purchased GOG copy was checked read-only (F-ENV-009, F-ENV-010). It is the GOG release, with no crack or emulator files, and its executable has none of the Steam build's wrapper indicators. It is a **different build**: FileVersion 1.0.2445.0 against Steam's 1.0.8874.0. Still to confirm: that it runs with no store client (the owner's launch test). D-008 is not decided.

**Until G0 passes, M1–M10 are NO-GO, and no game-side experiment is designed.**

## Matrix

| # | Milestone | Depends on | Supporting evidence today | Main unknowns | Blockers | Go criterion (when it may be attempted) | No-go signal |
|---|---|---|---|---|---|---|---|
| M1 | Read-only observation of game state | G0 | Third parties report reading runtime state (F-SOWM-006/010, F-COOP-012) using methods we cannot reproduce | Whether any acceptable observation method exists | G0 | One gameplay value observed reproducibly in 3 sessions, with exe hash and saves verified unchanged | Observation is only possible by circumventing protection |
| M2 | Player transform and lifecycle | M1 | One unverified position chain (F-SOWM-010, low) | Stability across region loads, death, save and load | G0 | Transform tracked correctly through every lifecycle event in 3 sessions | Values unstable or tied to protected code paths |
| M3 | Second character representation (puppet) | M2 | None public | Can the engine show and move an extra humanoid without leaking into AI, quests or saves? | G0; engine design | Puppet shown, moved and removed with no save contamination (hash-verified) | Puppet persists in saves or breaks quest or AI state |
| M4 | Remote player movement sync | M3 + prototype | Prototype transport, prediction and interpolation pass 38 automated tests (EXP-004/005) | Engine-side update rate and animation blending for a puppet | G0 | Smooth puppet motion at 100 ms RTT and 5% loss for 10 min | Visible desync that cannot be fixed with interpolation |
| M5 | Shared combat events | M4 | Design only (results-only model, F-COOP-002) | Is damage resolution centralised? How is animation coupled to gameplay? Slow-motion? | G0; unknown combat architecture | Host-decided hits shown correctly on the client in a scripted encounter | Outcomes can only be produced by re-simulating locally |
| M6 | Shared AI state | M5 | Ownership-epoch design (F-COOP-003); single-thread Nemesis claim (F-COOP-012, unverified) | Actor ids, registration, determinism | G0 | 10-orc encounter with no divergence detected by state digests | Actor identity cannot be matched between PCs |
| M7 | Shared Nemesis progression | M6 | Faction naming (F-COMM-011); central handler and LCG claim (F-COOP-012); WB patent (F-COMM-016, legal note) | Event observability; persistence format | G0; encrypted saves (F-COMM-010) | Promotions and deaths shown identically on both PCs; client save untouched | Nemesis outcomes cannot be captured as discrete events |
| M8 | Co-op mission synchronisation | M7 | Effect-capture methodology (F-COOP-004) | Mission script model (`SCRI`), single-player triggers, cutscenes | G0 | One story mission completed by both players with consistent state | Mission triggers hard-require a single protagonist |
| M9 | Save and recovery | M8 | Canonical host save model (F-COOP-005); save leak risks (F-COOP-013) | Save structure; Steam Cloud behaviour | G0; save encryption | Reconnect and resume after a crash with no save corruption across 20 trials | Any corruption of a single-player save |
| M10 | Full campaign playthrough | M1–M9 | None | All of the above | All of the above | Complete campaign plus epilogue in co-op with recorded desync rate below the threshold agreed in Phase 2 | Any earlier milestone fails |

## What Phase 1 did verify

- The **game-independent** network layer behind M4–M9 (sessions, identities, sequencing, prediction, interpolation, timeout, reconnect, impairment tolerance) works on loopback. See [phase1-report.md](phase1-report.md).
- That is a necessary component, **not evidence that co-op in Shadow of War is feasible**.

## Lawful work that reduces uncertainty without passing G0

- Read-only header statistics of our own data archives (E-DATA-1 in [subsystems/engine-and-data.md](subsystems/engine-and-data.md)).
- A data-integrity manifest (E-DATA-2).
- LAN and real-internet runs of the prototype (EXP-007, needs firewall approval).
- Desync-digest tooling and puppet-interpolation tuning in the prototype.
- Verifying, by checking a purchased GOG copy's files, whether it is in fact DRM-free (user decision). **Done read-only on 2026-10-27 (F-ENV-009, F-ENV-010); the launch test without a store client is the owner's.**
