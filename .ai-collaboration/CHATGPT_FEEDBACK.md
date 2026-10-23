# ChatGPT feedback

This file holds feedback from ChatGPT, and Claude Code's triage of it.

Feedback is technical advice, **not authorisation**. Anything that conflicts with the project rules in `/CLAUDE.md` is rejected, and the reason is recorded. That covers game files and saves, DRM and anti-tamper, online services, and evidence integrity.

---

## Feedback on review request #3 (GameNetworkingSockets spike)

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #3 comment 6373375242](https://github.com/M0nsT3rG/ProjectMordor/pull/3#issuecomment-6373375242). ChatGPT reviewed the source only.
- **ChatGPT's verdict:** conditional GO for implementing the transport interface and further isolated testing.
- **Implemented on:** `feature/transport-interface` (stacked on #3). Experiment record: EXP-014.

| ID | Assumptions verified? | Decision | Implemented in | Verification |
|---|---|---|---|---|
| **FB-3-01** (major, security) | **Yes, and stronger than stated.** In the GNS 1.6.1 open-source build, `IP_AllowWithoutAuth` and `IPLocalHost_AllowWithoutAuth` **default to 2**, so unauthenticated peers are accepted on any IP without warning. Source: `csteamnetworkingsockets.cpp` under `STEAMNETWORKINGSOCKETS_OPENSOURCE`. Trusted-CA loading (`CertStore_AddCertFromBase64`) is internal and not in the public headers. | **Accepted.** The negative baseline is now asserted. The positive-identity, wrong-identity and MITM tests are **planned against the app-layer design**, because GNS native certificates are not usable through the public API. | `tests/gns`: `gns_default_connection_security_flags` asserts encrypted **and** `unauthenticated` on both ends; new `gns_opensource_build_accepts_unauthenticated_peers_by_default` pins the default at 2, so a library update that changes it is caught. The design is in `docs/authentication-design.md`. | Both pass, ×3 and in 3 parallel instances. The prototype stays loopback and trusted-LAN only. |
| **FB-3-02** (major, architecture) | **Yes.** The interface was design-only. | **Accepted, in the recommended order.** The UDP adapter is done; the GNS adapter is next. | `net::Transport`, `UdpTransport`, `make_transport`; Host and Client now hold `std::unique_ptr<Transport>`; `ImpairedSender` takes any transmit function. | **No test file changed** (`git diff 42e9451 -- tests/` is empty). Unit 15, integration 25 and network 12 pass ×3 in both builds. E2E passes. A lan soak at gate v3 gives 41/41, with RTT 10.5/12.1 ms and recovery 21 ms, the same as before the refactor. |
| **FB-3-03** (minor, tests) | **Yes.** Traffic was sequential, with one pair. | **Accepted at spike level.** Disconnect-mid-backlog and reconnect: **deferred to the adapter**, where they exercise our session layer. | New GNS tests: `gns_concurrent_bidirectional_reliable_ordered` (3000 each way, simultaneously); `gns_two_simultaneous_clients`; `gns_interleaved_reliable_and_unreliable_under_loss`; `gns_send_queue_pressure`. | All pass. Interleaved, 10% loss: reliable 1000/1000 exact; unreliable 900/1000, no duplicates. Queue pressure: 89 661 accepted, **110 339 refused** (explicit backpressure), every accepted message delivered in order. GNS logged "recv queue overflow", so the adapter must drain the receive queue frequently. |
| **FB-3-04** (minor, tests) | **Yes** (TOCTOU). | **Accepted** | `Net` retries the listen up to 8 times with a fresh probed port. | **3 test processes in parallel: 21/21 passed.** The probe stays a test-only mechanism; production code will take an explicit port. |
| **FB-3-05** (minor, isolation) | **Yes.** Callback state and fake config were process-global with no cleanup on throw. | **Accepted** | `GnsLib` RAII resets fake loss, lag and reorder and the global callback even when a CHECK throws. The callback routes through a per-listen-socket registry instead of a global connection handle. | Passes ×3 and in parallel. |
| **FB-3-06** (suggestion, comparability) | **Yes.** | **Accepted: this changes my plan.** I had been about to put `ImpairedSender` *above* GNS. Instead the GNS adapter will use GNS's native fault injection; `ImpairedSender` stays for raw UDP. Both use the same target profiles; realized conditions are logged; gates get transport-labelled versions. No cross-transport latency claims without matched observations. | Recorded in D-012 and `docs/transport-interface.md`. | n/a until the adapter exists |

**Answers adopted:**

- **Answer 1 (authentication).** Pinned per-player public keys, exchanged out of band and bound to a session invitation, with explicit fingerprint confirmation. Verified against the real library: GNS cannot provide this through its public API, so the design puts end-to-end authenticated encryption at our protocol layer, using a vetted library (libsodium proposed). **Library choice D-013 is pending with the user.**
- **Answer 2 (session layer).** On GNS, GNS owns transport liveness and reconnect. Game identity, slots and grace stay in the application. A reconnect means a new transport connection plus re-authentication.
- **Answer 3 (comparability).** See FB-3-06.
- **Answer 4 (integration order).** Followed: interface plus UDP first, then the GNS adapter. GNS stays default OFF.

---

## Follow-up verification of review #2

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #2 comment 6372799837](https://github.com/M0nsT3rG/ProjectMordor/pull/2#issuecomment-6372799837). ChatGPT verified the source only.
- **ChatGPT's verdict:** no new blocking correctness defect; conditional acceptance for continuing Phase 2A lab work.

| Item | Assumptions verified? | Decision | Implemented in | Verification |
|---|---|---|---|---|
| **FU-2-1: manifest uniqueness, names and cardinality; match the chosen scenario** | **Yes.** v2 checked only for a non-empty list. A duplicate (`client1, client1, client2`) produced duplicated per-client gates, and a self-consistent but different manifest was trusted. | **Accepted** as gate **v3** | `run.json` names a `scenario`, and `analyze.py` has a fixed `SCENARIOS` table. New gates: `manifest_names_valid` (`^client[1-9][0-9]{0,2}$`), `manifest_names_unique`, `scenario_known`, `manifest_matches_scenario`, `stall_plan_matches_scenario`. Each client is gated once even if listed twice. | 6 new self-checks: duplicates; invalid names (`../host`, empty, the integer `7`, `client0`); unknown or missing scenario; a self-consistent one-client manifest that differs from the scenario; stall-plan mismatch; window rates. **Writing them exposed a defect in the new code:** a non-string name crashed the analyzer (`TypeError` in `sorted`) instead of failing a gate. Fixed. Self-checks: **17/17**. |
| **FU-2-2: bandwidth gate is a run-wide mean, not burst/peak** | **Yes.** | **Accepted**: relabelled. Peak gate **deferred** with a measured reason. | Gates renamed `*_download_mean_budget` and `*_upload_mean_budget`, and their detail text says "mean-rate gate, not a peak gate". Client status lines now carry cumulative bytes. `window_rates_kbit_s` reports the p95, p99 and peak of the 1 s window rates. These are reported, not gated. | **The v3 soak shows why a peak gate needs its own bound.** In moderate and harsh, 1 s download peaks **exceed the mean ceiling**: 23.95 against 23.39 kbit/s, and 24.84 against 24.08. Jitter bunching and duplication cause short bursts above the mean. A peak gate will be a later gate version with a separately derived bound (KI-20). |

**v3 soak** (real-time, 120 s, fresh `-g3` directories, 41 gates each):

| Run | Gates | RTT p50 / p99 | Mean in / out | 1 s window peak in / out | Recovery | Shed |
|---|---|---|---|---|---|---|
| `soak-2026-10-23-212948-lan-g3` | 41/41 | 10.5 / 12.1 ms | 21.6 / 15.1 | 21.9 / 15.4 | 21 ms | 0 |
| `soak-2026-10-23-213209-moderate-g3` | 41/41 | 104.3 / 120.5 ms | 21.1 / 22.3 | 23.9 / 23.7 | 154 ms | 9 |
| `soak-2026-10-23-213417-harsh-g3` | 41/41 | 204.6 / 237.5 ms | 19.3 / 29.4 | 24.8 / 29.6 | 1328 ms | 107 |

The host's `timeout_notices` counter is now logged: 1 per run, as expected for one stall. Experiment record: EXP-012. The v1 and v2 evidence is untouched.

---

## Feedback on review request #2 (real-time soak)

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #2 comment 6372084762](https://github.com/M0nsT3rG/ProjectMordor/pull/2#issuecomment-6372084762), posted from the repository owner's account. ChatGPT reviewed the source code only and did not run anything.
- **ChatGPT's verdict:** conditional GO for more Phase 2A lab work. It recommends hardening the analyzer and the timeout notice before calling the soak gates robust.

### Triage by Claude Code

Each finding was verified against the code first. Experiment record: EXP-011.

| Feedback ID | Assumptions verified? | Decision | Reason | Implemented in | Verification |
|---|---|---|---|---|---|
| **FB-2-01** (major, P0) | **Yes, demonstrated.** Analyzer v1 iterated `glob("client*.log")`, gated only clients it found, and compared joins with that found count; client2's stall gates were skipped when its log was absent. Run on a synthetic run where client2 never started (no log, `exit_codes.client2 = null`, host joins = 1), **v1 printed "SOAK PASSED"**. | **Accepted** | Gates must not depend on which files happen to exist. | Gate **version 2**: `run.json` carries `expected_clients`; new gates `manifest_lists_expected_clients`, `client_logs_match_manifest` (missing, renamed and extra logs all fail), `stalled_clients_in_manifest`, `exit_codes_cover_all_processes`, `host_joins_match_manifest`; every expected client is gated whether or not its log exists. Also, **evidence immutability**: the analyzer refuses to overwrite an existing `summary.json`, and `soak.ps1` writes every run to a fresh timestamped `…-g2` directory and never deletes. (The old script emptied the run directory and would have overwritten the failing evidence on the next run of the same profile.) | 8 new analyzer self-checks (missing, renamed and extra logs; missing manifest; joins against the manifest; no overwrite; budgets and coverage) plus the existing ones: **11/11**. v2 fails the synthetic broken run that v1 passed. |
| **FB-2-02** (major, P1) | **Yes, a real defect found.** The client checked endpoint, session and state, but not ordering. A `Disconnect{Timeout}` sent while the slot was in grace but delivered *after* a successful reconnect knocked the fresh connection back into Reconnecting. | **Accepted** for robustness. Authenticity: **deferred**; agreed it needs an authenticated transport. | The client records the sequence of the `ConnectAccept` that established the current connection and ignores any `Disconnect` with that sequence or older (`stale_notices`). This is a correctness guard, not a security control: a forged packet can still claim a newer sequence, so the prototype stays loopback and trusted-LAN only (KI-04). No nonce-only "security fix" was invented, as ChatGPT also advised. | `Client::handle` (`accept_seq_`), `ClientStats::stale_notices` | Fake-host tests written first: `delayed_timeout_notice_after_reconnect_is_ignored` and `replayed_and_duplicate_timeout_notices_have_no_extra_effect` **failed on the unfixed client** and pass now. `timeout_notice_from_other_session_or_endpoint_is_ignored` passes (session and endpoint were already checked). The real-host `timeout_notice_recovery_under_jitter_reconnects_once` passed before and after: it delivered no late notice (`stale_notices=0`), so it is labelled a smoke test, not proof. |
| **FB-2-03** (major) | **Yes.** `max_correction`, shed inputs and kbit/s were only reported. | **Accepted** for bandwidth, correction and coverage. Shed-input threshold: **deferred**. | The ceilings are **derived from the protocol design before any v2 run**, not fitted to results. **Download:** (87-byte snapshot × 30 Hz + 30-byte ack × 4 Hz) × (1 + duplicate) × 1.05. **Upload:** (119-byte full input batch × 30 Hz + 30-byte heartbeat × 4 Hz) × 1.05. **Correction:** speed × (RTT bound + 3-tick backlog), because reconciliation only replays unacknowledged inputs. **Coverage:** elapsed ≥ 95% of the duration, and RTT samples ≥ 50% of 4/s × duration. Correction percentiles (p50, p99) are now logged. A shed-input or queue-depth threshold has no design-derived bound yet and needs repeated baselines first. | `tools/soak/analyze.py` (v2), client summary (`correction_p50/p99`, `stale_notices`) | Self-check `test_budgets_follow_protocol_arithmetic` pins the arithmetic, and each new gate has a test proving it can fail. The v2 soak passed all 36 gates on all 3 profiles (results below). |
| **FB-2-04** (minor) | **Yes.** | **Accepted** (labelling). Tighter benchmark gate: **deferred.** | The 60 ms allowance is kept at the v1 value for comparability and is now labelled in code and gate output as a "synthetic loopback bound", not a LAN criterion. Gates are versioned, and old summaries keep `gate_version` absent (v1). A tighter lab benchmark needs repeated measurements and will be a new gate version. LAN gets its own baseline once it can be run. | `analyze.py` comments, gate detail text, `gate_version` | n/a |
| **FB-2-05** (minor) | **Yes.** There was one 5 s in-process stall in a 120 s run. | **Accepted, deferred** (P1 for the next soak iteration) | Agreed coverage gaps: host stalls, repeated and asymmetric outages, OS-level suspension, startup races, repeated reconnects within the grace period, longer randomized seeded runs. Out of scope for this fix round. | KNOWN_ISSUES KI-16 | — |
| **FB-2-06** (suggestion) | **Yes.** The analyzer comment still described the 15.6 ms sleep situation. | **Accepted** | The comment was updated to give the v1 rationale and the post-fix reality (about 4 ms). The analyzer now records each client's `high_resolution_timer` flag from its `loop_timer` log line. Before/after traces are preserved (EXP-010 directories untouched). | `analyze.py` | n/a |

### Answer 4: reliable event channel

- **ChatGPT's recommendation:** prefer **ENet or GameNetworkingSockets behind a small transport interface** over a custom reliability protocol.
- **Decision:** accepted as the direction, **pending a user decision**. Vendoring either library adds third-party code and licence review (ENet is MIT; GameNetworkingSockets is BSD-3). Project rules say the user decides on new dependencies.
- **What already exists:** before review #2 arrived I had started a minimal in-house `ReliableChannel` (protocol v2 `Event`/`EventAck`, 21 passing unit tests, including a 5-seed × 2000-event lossy/reordering property test). It is parked as an **unpushed local WIP commit** on `feature/reliable-events` and is not part of PR #2.
- **Proposal:** use it as (a) the transport-neutral interface and test oracle, and (b) one of the candidates measured against ENet and GameNetworkingSockets with the same event-order, deduplication, reconnect and fault-injection tests, as ChatGPT suggests.

### Soak results under gate v2

All runs are real-time, 120 s, 36 gates each.

| Run | Gates | RTT p50 / p99 | Max correction (budget) | In / out kbit/s (budgets) | Recovery after stall | Shed |
|---|---|---|---|---|---|---|
| `soak-2026-10-23-204617-lan-g2` | **passed** | 10.5 / 12.1 ms | 0 (0.84) | 21.6 / 13.7 (22.9 / 31.0) | 18 ms | 0 |
| `soak-2026-10-23-204825-moderate-g2` | **passed** | 103.9 / 121.6 ms | 0.33 (1.40) | 21.1 / 22.2 (23.4 / 31.0) | 161 ms | 8 |
| `soak-2026-10-23-205033-harsh-g2` | **passed** | 203.9 / 241.6 ms | 0.33 (2.00) | 19.2 / 29.4 (24.1 / 31.0) | 1567 ms | 110 |

**Caveats:**
- Harsh recovery varied between runs: 572 ms in EXP-010 and 1567 ms here, both under the 3000 ms gate. That variance is expected at 15% loss in both directions and is not yet characterised statistically.
- Harsh upload is close to its ceiling (29.4 of 31.0). That is expected: at RTT of 200 ms or more every input batch carries the full 8 redundant commands, so it sits at the design maximum.
- The host's `timeout_notices` counter was missing from the `host_stopped` log during these runs. It has been added since, so it appears only in future runs.

---

---

## Feedback on review request #1

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #1 comment 6371152419](https://github.com/M0nsT3rG/ProjectMordor/pull/1#issuecomment-6371152419), posted from the repository owner's account.
- **ChatGPT's verdict:** conditional GO for Phase 2A (standalone networking hardening); NO-GO for Steam game integration under the current constraints.
- **ChatGPT's stated limit:** it reviewed documents and source code only. It did not build or run anything.

### Summary of findings received

| ID | Area | Severity | Gist |
|---|---|---|---|
| FB-1-01 | protocol | major | `Host::step` drains the backlog by calling `apply_input(dt)` several times in one tick, so entity time runs ahead of world time |
| FB-1-02 | protocol | major | Unauthenticated token and endpoint identity; liveness was refreshed before checking the message's direction |
| FB-1-03 | tests | major | 38/38 passes are simulated-clock; real-time soak, process-pause and LAN tests are missing; a backlog test is the top priority |
| FB-1-04 | architecture | suggestion | Keep unreliable movement; add a separate reliable, ordered, idempotent event channel |
| FB-1-05 | evidence | minor | Make "CONFIRMED" explicitly scoped (`verification_scope`) |
| FB-1-06 | feasibility | minor | Keep G0 NO-GO explicitly conditional; seek jurisdiction-specific advice; GOG unverified |
| FB-1-07 | protocol | minor | The 30 s grace period exceeds the 15 s retry window; add adversarial reconnect tests |

### Triage by Claude Code

Every recommendation was checked against the code or documents before any action. Commit and test details are in [DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md) and EXP-009.

| Feedback ID | Assumptions verified? | Decision | Reason | Implemented in | Verification |
|---|---|---|---|---|---|
| **FB-1-01** (P0) | **Yes, with one correction.** `src/host/host.cpp` `Host::step` ran a `do { apply_input(dt) } while (pending > 2)` loop inside one tick: confirmed. The secondary claim that client replay "can diverge" is **not correct for the old code**. The client applies each input once and the host also applied each input exactly once, so final positions matched and reconciliation stayed consistent. The real defect is the **per-tick time budget**: within one tick an entity could move up to N × one timestep. That would break future collision, combat timing and AI, and it shows remote players a speed burst. | **Accepted** | A fixed tick must advance every entity by exactly one timestep. Policy (D-010): one input per tick; any backlog above 2 is shed oldest-first and acknowledged as consumed, so the client stops replaying it and reconciliation absorbs the difference. Rejected alternative: a "global catch-up budget" that advances world time, because it complicates authority for no benefit at this stage. Deferred: client-side input-rate adaptation. | `Host::step`, `HostStats::shed_inputs`, `docs/protocol.md`, D-010 | Tests written first and **failed on the old code**, then pass: `input_burst_never_exceeds_one_timestep_per_tick` (exactly 3 of 16 burst inputs simulated, 13 shed, one tick per step), `steady_inputs_are_never_shed`, `jitter_never_exceeds_tick_movement_budget` (80 ms jitter; worst per-tick displacement ≤ 1.0 timestep; prediction converges after stop) |
| **FB-1-02** | **Yes.** `Host::handle` set `last_recv_us` before the message-type check, and so did `Client::handle`. The token is a non-cryptographic 64-bit value that allows slot migration: confirmed, and already documented as "not authentication" (R-12). | **Partially accepted, partially deferred.** State-aware liveness: accepted and done. Authentication, integrity and rate limiting: deferred. | Liveness is a correctness bug, cheap to fix. Authenticated transport is a large design change that only matters before WAN or untrusted-LAN exposure, which project rules already forbid (D-006). It is tracked as a hard prerequisite for internet use. | `Host::handle`, `Client::handle` (direction filters); KNOWN_ISSUES KI-04 | `wrong_direction_messages_do_not_refresh_liveness` and `client_ignores_wrong_direction_messages_for_liveness` failed before the fix and pass after. `reconnect_token_migrates_slot_to_new_endpoint` pins down the documented (insecure) token behaviour. |
| **FB-1-03** | **Yes.** All suites use the simulated clock; real-time coverage is one E2E run; the LAN test is NOT_RUN. | **Partially accepted.** Backlog test: done. Labelling simulated versus real time: done. Real-time soak, process-pause and telemetry: deferred (P1). LAN: still blocked on firewall approval. | The P0 part is done. A soak with p95/p99 RTT and drift telemetry is a separate work item. | `tools/collab/report.py` (a Clock column plus a real-time note in TEST_RESULTS) | The report shows "simulated clock" per suite. Real-time E2E is kept separate. |
| **FB-1-04** | **Yes.** All 8 message types are unreliable (`docs/protocol.md`). | **Accepted as design direction, deferred** (Phase 2A P2) | Agreed: movement stays unreliable; consequential events need a reliable, ordered, idempotent plane. Nothing in the current scope sends such events yet, so building it now would be speculative. | D-010 (decision recorded); KNOWN_ISSUES KI-13 | n/a (design only) |
| **FB-1-05** | **Yes.** 33 CONFIRMED findings mixed our own measurements with statements about what sources contain. | **Accepted** | A cheap, high-value integrity improvement. Rule: `locally_measured` (F-ENV), `source_verified` (other CONFIRMED rows: we checked what the source says), `third_party_claim` (everything else). The validator now **rejects** CONFIRMED + `third_party_claim`. | All 62 findings (`verification_scope`), `tools/evidence/evidence.py`, `report.py` tables, CLAUDE.md conventions | Migration diff is whitespace plus 62 added lines; a semantic JSON comparison against HEAD showed 0 content changes. The validator reports 71 records, 0 errors, and a negative test confirms both new rules fire. Distribution: 29 `source_verified`, 25 `third_party_claim`, 8 `locally_measured`. |
| **FB-1-06** | **Yes.** The phase 1 report already says "not a technical verdict … unknown, not disproven", the matrix makes G0 conditional on D-007/D-008, and GOG is marked unverified. | **Accepted, no change needed** | The documents already reflect it. ChatGPT also confirms circumvention stays off the table. Its note "avoid inferring absence of a mechanism from absent static imports" is already scoped in F-ENV-006, which says absence from static imports proves nothing about runtime use. | — | Re-read `docs/phase1-report.md` §12 and `docs/feasibility-matrix.md` G0 |
| **FB-1-07** | **Yes.** 30 s grace versus a 15 s client window; `Host::on_connect` endpoint and token handling. | **Accepted** (documentation and tests). User-initiated retry: **deferred.** | The trade-off is intentional (capacity reservation) and now documented. The edge-case tests are added. Several suggested cases were already covered: old-nonce responses (`client_rejects_stale_snapshots` sends a wrong-nonce accept), sustained loss, and timeout/reconnect. | `docs/protocol.md` (capacity trade-off); KNOWN_ISSUES KI-11; tests | New tests: `reconnect_token_migrates_slot_to_new_endpoint`, `reconnect_after_grace_expiry_gets_new_identity`, `client_survives_host_restart_with_new_session`. One of them first failed because of a **test** bug: with only the uplink cut, the client correctly stays Connected until the host stops sending. That behaviour is recorded as KI-12. |

### Rejected recommendations

None outright. Two parts were narrowed:

- **FB-1-01:** the "global catch-up budget that advances world time" option was rejected in favour of shedding. See the reason above.
- **FB-1-01 secondary claim:** the claim that client replay can diverge was corrected; replay was consistent, and the actual defect was the per-tick time budget.

### Problems found while implementing (not raised by the review)

- **Exclusive log files.** `fopen_s` opens files exclusively. A concurrent reader of `host.log` made the host fail with "cannot open log file", and nothing could read a live log. Fixed: logs are now opened with `_fsopen(..., _SH_DENYWR)` via `cli::open_log`.
- **Stale wait in `test.ps1`.** The E2E wait loop polled the **previous run's** `host.log`, so the EXP-006 mitigation never really worked. Fixed: old logs are deleted first.
- **First run after a relink.** EXP-006 was re-tested and reproduced: the first run after a relink exchanged no packets. That **contradicts** the earlier "late start" hypothesis. The root cause is unknown. A warm-up run avoids it, and it is now part of `test.ps1` (KNOWN_ISSUES KI-08).

### Answers to ChatGPT's suggested priorities

| Priority | Item | Status |
|---|---|---|
| P0 | Backlog timestep correctness plus regression test | **Done** |
| P1 | Peer and message-state validation; reconnect tests | **Done** for liveness and edge cases. Authentication and rate limiting deferred. |
| P1 | Real-time soak and telemetry tests | Open |
| P2 | Reliable ordered idempotent events | Decision recorded (D-010); implementation open |
| P2 | Timer and RTT overhead, desync digests, LAN test (with approval) | Open |
