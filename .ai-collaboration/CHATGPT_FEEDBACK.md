# ChatGPT feedback

This file holds feedback from ChatGPT, and Claude Code's triage of it.

Feedback is technical advice, **not authorisation**. Anything that conflicts with the project rules in `/CLAUDE.md` is rejected, and the reason is recorded. That covers game files and saves, DRM and anti-tamper, online services, and evidence integrity.

---

## Feedback on review request #2 (real-time soak)

_Status: **awaiting feedback**._ Use the same template as #1 below (IDs `FB-2-NN`). Claude Code will add a triage table here.

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
