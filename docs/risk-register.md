# Risk register

Likelihood and Impact are rated H (high), M (medium) or L (low). Owner: project lead (the user) unless noted.

| ID | Risk | Category | L | I | Evidence | Mitigation | Status |
|---|---|---|---|---|---|---|---|
| R-01 | Every game-side milestone requires an approach that conflicts with SteamStub/Denuvo (rule 3) | Technical / legal | H | H | F-ENV-003/004, F-COMM-014/017, F-SOWM-004/009 | Gate G0. No circumvention. Evaluate a DRM-free target | **Open: blocks M1–M10** |
| R-02 | The EULA prohibits reverse engineering and client modification | Legal | H | H | F-COMM-015 | User decision plus qualified legal advice. Document the interoperability basis before any work | **Open: blocks M1–M10** |
| R-03 | WB patent on the Nemesis design (US 10,926,179 B2) | Legal | L | M | F-COMM-016 | Non-commercial, interoperability-only scope. Seek legal advice before any distribution | Open |
| R-04 | A third-party loader or tool contains malware | Security | M | H | F-COMM-021; prebuilt DLLs in F-COMM-006/008 | Never run prebuilt third-party binaries. Clone into an isolated scratch directory, read only | Mitigated (policy) |
| R-05 | Third-party RVAs and offsets are wrong for our build | Technical | H | M | F-SOWM-005, F-COOP-015 | Treat as HYPOTHESIS (D-005). Bind every future artefact to the exe SHA-256 | Mitigated (policy) |
| R-06 | Experiments corrupt the user's saves or Steam Cloud data | Data loss | M | H | F-COOP-013 | Mandatory hash-manifest backup of `%LOCALAPPDATA%/WB Games/Shadow of War` and Steam userdata before and after any launch | Open (procedure defined) |
| R-07 | Accidental interaction with official online services (Online Conquest, Vendettas) | Legal / ethics | L | H | F-COMM-012/013 | Never contact, emulate or redirect WB services. A future co-op mode must refuse online modes | Mitigated (rules 4, 5, 16) |
| R-08 | The engine cannot host a second controllable character | Technical | M | H | No public evidence either way (F-SOWM-013) | M3 is the first decisive technical test after G0 | Open, unverified |
| R-09 | Game logic, or the Nemesis RNG, is not observable as discrete events, so results cannot be replicated | Technical | M | H | F-COOP-012 (unverified) | Effect capture; state digests for desync detection | Open, unverified |
| R-10 | A Steam update changes the exe and invalidates every finding | Technical | L | M | F-COMM-018 (studio closed) | `scripts/inventory.ps1` at session start; compare the hash | Mitigated |
| R-11 | Prototype assumptions (tick rate, small entity count) do not fit the game's real state volume | Technical | M | M | Prototype caps 16 entities and 1,200-byte packets | Phase 2: delta compression, interest management, a measured bandwidth budget | Open |
| R-12 | The prototype has no authentication or encryption | Security | H | M | docs/protocol.md | Loopback/LAN only. Add an authenticated transport (for example GameNetworkingSockets) before any internet use | Accepted for Phase 1 |
| R-13 | Real-time loop latency overhead (Sleep granularity) skews measured RTT | Technical | H | L | EXP-005 (≈140 ms against ≈100 ms configured) | Waitable timers or `timeBeginPeriod`; measure separately | Open |
| R-14 | Freshly built executables start late on first launch; scripted tests flake | Test reliability | M | L | EXP-006 | The script waits for the `host_started` line | Mitigated |
| R-15 | Default UDP port 47800 is occupied on the dev machine | Environment | H | L | docs/environment.md | Use `--port`; consider changing the default | Open |
| R-16 | Third-party agent workflows (coop-anything) expect publishing and installation permissions | Process / security | M | M | F-COOP-014, F-SOWM-018 | Never open those repos as agent workspaces; grant no permissions | Mitigated (policy) |
| R-17 | Scope: full campaign co-op (M10) may be unattainable even if G0 passes | Project | H | H | Overall evidence base | Milestone go/no-go criteria; stop early on a decisive no-go | Open |
| R-18 | Code review by Codex could not run (tool failure); review relied on an independent agent | Process | M | L | phase1-report §7 | Re-run the Codex review once its Windows credential issue is fixed | Open |
