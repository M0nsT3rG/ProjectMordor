# Project Mordor: Phase 1 report

Date: 2026-10-23. Target: Middle-earth: Shadow of War, Steam app 356190, buildid 3095001, `ShadowOfWar.exe` 1.0.8874.0, SHA-256 `ED0D95CABB033CD66AAA99D699374881846C3CB03EC345F1123D82D45E0BDAEF`.

## 1. Executive summary

- **Inventory.** The installed game was inventoried read-only. No mod loader is installed. The game and the saves are unchanged: no writes were made, and the save directory was not even listed.
- **Prior art.** Three requested repositories and nine related ones were reviewed and cited at pinned commits: 62 findings plus 8 experiments in `evidence/`, all validated.
- **Networking prototype.** A standalone host-authoritative UDP prototype (C++20, Winsock2, no third-party dependencies) was built and tested. 38 automated tests pass on repeated runs, and a real-process end-to-end run under 40 ms latency, 20 ms jitter and 10% loss passed. **This prototype has never touched the game. It is not evidence that co-op in Shadow of War works.**
- **Decisive finding: game-side work is blocked.**
  - The Steam executable shows a protected layout (F-ENV-003/004).
  - Denuvo is still declared on the store page (F-COMM-014, F-COMM-017).
  - The only public deep-analysis workflow first strips the Steam wrapper (F-SOWM-009), which project rule 3 forbids.
  - The EULA forbids reverse engineering and client-interacting programs (F-COMM-015).
- **Recommendation.** **Conditional NO-GO for game integration. GO for a narrow Phase 2A** of decision gates and lawful groundwork (section 12).

## 2. Confirmed findings (CONFIRMED)

Facts we measured ourselves:

- **F-ENV-001:** exact build identity.
- **F-ENV-002:** no proxy loader DLLs.
- **F-ENV-005:** Wwise and Scaleform are statically linked.
- **F-ENV-006:** no static `ws2_32`; HTTP and Steam API imports.

Facts about sources (what they say or contain, not proof about the game):

- **SoWMods** is a `steam_api64.dll` proxy with hardcoded RVAs and no build check (F-SOWM-001, 003, 005, 006). It maps no combat, AI, Nemesis, mission or save systems (F-SOWM-013). It has no license (F-SOWM-017).
- **coop-anything** (MIT) provides the methodology: a host-authoritative puppet model, one owner per AI actor with epochs, effect capture, a canonical host save, and seeded impairment and desync-digest testing (F-COOP-001..010). It also contains a Shadow of War write-up claiming our exact hash (F-COOP-011..015).
- **The modding community org** has three unlicensed repos with partial format patterns and a mesh tool (F-COMM-001, 006).
- **The EULA** prohibits client modification, reverse engineering and third-party interaction (F-COMM-015). **A WB patent** covers the Nemesis design (F-COMM-016).

## 3. Unverified hypotheses (selection)

| Topic | Claim | Class | Source |
|---|---|---|---|
| Engine | Firebird/"Kraken", LithTech lineage, client/server split, NetMesh code | PARTIALLY_CONFIRMED / HYPOTHESIS | F-COMM-007, F-COOP-011, F-SOWM-008, F-ENV-007 |
| Protection | SteamStub plus Denuvo on the Steam build; the claim that Denuvo was removed is **CONTRADICTED** | HYPOTHESIS / CONTRADICTED | F-ENV-003/004, F-SOWM-004, F-COMM-014/017 |
| Player | Singleton → player → position chain | HYPOTHESIS, low | F-SOWM-010 |
| Nemesis | "Faction" system, single thread, central event handler, one global LCG | PARTIALLY_CONFIRMED / HYPOTHESIS | F-COMM-011, F-COOP-012 |
| Saves | `SOM3` AES-CBC files; Steam Remote Storage leaks | PARTIALLY_CONFIRMED | F-COMM-010, F-COOP-013 |
| Data | `LTAR` arch06 archives, Oodle; ten asset magics | PARTIALLY_CONFIRMED | F-COMM-002/003, F-SOWM-016 |
| DRM-free alternative | The GOG release has no DRM | PARTIALLY_CONFIRMED (single secondary source) | F-COMM-017 |

## 4. Existing modding tools and compatibility

| Tool | Mechanism | Compatibility with our build |
|---|---|---|
| SoWLoader (SoWMods) | `steam_api64.dll` proxy, MinHook, ImGui | **Unknown.** Hardcoded RVAs with no hash check. The exe size is consistent with ours, but that does not validate it (F-SOWM-005). |
| Middle-Earth-Mod-Loader | `bink2w64.dll` proxy, pattern scan | Unknown. Pattern scanning is more robust; untested by us (F-COMM-008). |
| PacketLoader | Resource swap via a file-read hook | Unknown (F-COMM-009) |
| sow-freecam | Camera hook via signature | Unknown (F-COMM-019) |
| Save editor / converter | Offline save tooling | Unknown; reported to need signature updates for 3095001 (F-COOP-015) |

None were installed or run. Running any of them would mean injecting code into the protected Steam process, which falls under gate G0.

## 5. Game architecture observations

See [architecture.md](architecture.md) and [subsystems/](subsystems/). In short:

- Every runtime question (game loop, entities, controller, combat, AI, missions) is UNKNOWN or HYPOTHESIS.
- The single most important technical unknown is whether the engine can host a second controllable character (M3).

## 6. Networking prototype results

Implemented in `src/` and specified in [protocol.md](protocol.md):

- explicit protocol version with a frozen header and frozen reject message;
- session ids; unique, never-reused player and entity ids;
- 2-player cap;
- wraparound-safe sequence numbers;
- stale and duplicate snapshot and input rejection;
- fixed-tick host authority;
- redundant input batches;
- client prediction with reconciliation;
- remote interpolation;
- heartbeat RTT (EWMA);
- 3 s timeout;
- reconnect token with a 30 s grace period;
- graceful disconnect (sent 3 times);
- seeded impairment (loss, latency, jitter, duplication);
- JSON-lines structured logs, with no IPs, paths or names;
- loopback-only by default.

End-to-end results (EXP-005, loopback, 40 ms latency, 20 ms jitter, 10% loss on every sender):

- both players joined and left cleanly;
- 0 invalid packets, 0 prediction corrections, 0 stale snapshots;
- RTT about 140 ms against about 100 ms configured. The surplus is attributed to the 1 ms `Sleep` loop granularity (R-13). That is not yet measured separately.

Bugs found by running the system and fixed:

- the `const char*` log field printed `true`;
- a single lost Disconnect;
- the review fixes listed in section 7.

## 7. Automated test results

| Suite | Tests | Result | Log |
|---|---|---|---|
| unit | 15 | 15 PASSED | `evidence/logs/tests-unit-2026-10-23.json` |
| integration (real loopback UDP) | 13 | 13 PASSED | `evidence/logs/tests-integration-2026-10-23.json` |
| network (impairment) | 10 | 10 PASSED | `evidence/logs/tests-network-2026-10-23.json` |
| end-to-end (3 real processes) | 1 | PASSED | `evidence/logs/e2e-2026-10-23/` |
| LAN between two PCs | — | **NOT_RUN** (one PC available; firewall change needs approval, EXP-007) | — |

How the required tests map to the suites:

| # | Requirement | Covered by |
|---|---|---|
| 1 | Host starts | `host_starts` |
| 2 | Client connects | `client_connects` |
| 3 | Unique identities | `two_clients_get_unique_identities` |
| 4 | Transform round-trip | `transform_snapshot_roundtrip`, `transform_replicates_between_clients` |
| 5 | Stale updates rejected | `sequence_newer_handles_wraparound`, `client_rejects_stale_snapshots` |
| 6 | Interpolation | `interpolation_is_consistent`, `interpolation_takes_short_path_for_yaw` |
| 7 | Delayed packets | `delayed_and_reordered_packets_converge`, `latency_is_measured` |
| 8 | Packet loss | `packet_loss_does_not_break_session`, `heavy_loss_and_duplication_never_crash` |
| 9 | Reconnect | `timed_out_client_reconnects_with_same_identity`, `reconnect_gives_up_after_window`, `silent_host_loss_ends_in_failed_state`, `expired_grace_period_removes_entity` |
| 10 | Version mismatch | `version_mismatch_is_reported`, `protocol_version_mismatch_is_rejected` |
| 11 | Invalid sizes and payloads | `decode_rejects_*`, 200,000-case fuzz, `host_survives_invalid_datagrams` |
| 12 | Clean shutdown | `clean_shutdown_notifies_clients_and_releases_socket` |

**Code review (EXP-008).**

- The Codex review could not run: Codex failed at startup with a Windows DPAPI error.
- An independent reviewer agent found nothing critical. It found 7 valid defects, which were fixed:
  - 16-bit id wrap;
  - input backlog ratchet;
  - retransmits not refreshing liveness;
  - grace-slot hijack by a token-less request;
  - stale ConnectAccept accepted;
  - two weak tests.
- A regression test was added for the id wrap.
- **Known gap:** no dedicated test covers the input-backlog drain.

**Caveat.** The tests drive a simulated clock over real loopback sockets. Real-time behaviour is covered only by the single end-to-end run.

## 8. Risks and blockers

Full list in [risk-register.md](risk-register.md). The blockers:

- **R-01** (protection versus rule 3) and **R-02** (EULA): together these are gate G0, which blocks M1–M10.
- **High-impact open risks:** R-06 (save safety), R-08 (second character), R-09 (event observability), R-17 (scope).

## 9. Missing evidence

- Any runtime observation of the game.
- A verified protector identity. Not pursued, by design (EXP-003).
- A verified archive or index layout.
- The save structure.
- Whether the GOG release is DRM-free, and whether it is the same game build.
- Nexus Mods and Discord knowledge (Nexus returned HTTP 403).
- LAN and internet prototype runs.
- A Codex review.

## 10. Recommended Phase 2 experiments (2A: no game interaction)

1. **Decision D-007 (user):** legal position on EULA versus interoperability law, with qualified advice. Record it in `docs/decisions.md`.
2. **Decision D-008 (user):** stay on Steam (G0 is likely to remain blocked) or evaluate a DRM-free release. If DRM-free, verify by **inspecting the purchased copy's files with the same read-only inventory** (no execution needed).
3. **EXP:** LAN run of the prototype on two PCs, with a scoped temporary firewall rule approved by the user.
4. **EXP:** real-time timer fix (waitable timers) and an RTT-overhead measurement.
5. **EXP:** desync digests and delta snapshots in the prototype; a bandwidth budget at 16 entities and 30 Hz.
6. **EXP E-DATA-1/2:** read-only archive header statistics and a data-integrity manifest of our own install.
7. **Tooling:** save-protection hash-manifest script, required before any future game launch.
8. **Process:** fix Codex's Windows credential issue and run the review again.

## 11. Exact reproduction commands

```powershell
./scripts/inventory.ps1                 # read-only discovery -> evidence/game-build.json, pe-inventory.json; validates + indexes evidence
./scripts/build.ps1                     # cmake -S . -B build; cmake --build build --config Release
./scripts/test.ps1 -Repeat 3            # ctest x3; copies JSON results to evidence/logs/
./scripts/test.ps1 -E2E                 # + real host and two clients on 127.0.0.1 with impairment
python -I tools/evidence/evidence.py validate
python -I tools/evidence/evidence.py index   # -> evidence/index.json, evidence/reports/index.md
build/Release/mordor-host.exe --port 47811 --latency-ms 40 --loss 0.1
build/Release/mordor-client.exe --port 47811 --pattern circle --duration-s 10
```

Port 47800, the default, is occupied on the development machine; use `--port`.

## 12. Final go/no-go recommendation

- **Game integration (M1–M10): NO-GO** under the current rules for the Steam build. This is not a technical verdict that co-op is impossible. With the evidence available, every path to the game's internals runs through protection we must not circumvent, under a EULA that prohibits it. Feasibility remains **unknown**, not disproven.
- **Phase 2A: GO.**
  - The two user decisions (D-007 legal, D-008 target platform) are the critical path.
  - The networking, testing and data-integrity groundwork listed above is lawful.
  - That groundwork stays useful whichever way the decisions go.
- **Re-evaluate G0** once D-007 and D-008 are recorded. Even if G0 passes, the first decisive technical test is M3 (second character). If M3 fails, stop before investing in M4–M10.
