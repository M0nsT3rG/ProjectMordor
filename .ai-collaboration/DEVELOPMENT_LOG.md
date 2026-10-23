# Development log

Newest entries first. Commits are short hashes on the branch named in each entry.

## 2026-10-23: ChatGPT review #2 triage and fixes (`feature/realtime-soak`)

- **Input:** [PR #2 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/2#issuecomment-6372084762), findings FB-2-01 through FB-2-06. The triage is in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md).
- **Changes:**
  - **Analyzer gate v2 (FB-2-01, FB-2-03):** exact participant manifest; coverage minimums; bandwidth and correction ceilings derived from the design.
  - **Evidence immutability:** `soak.ps1` writes fresh timestamped run directories, and the analyzer never overwrites a `summary.json`.
  - **Client stale-notice guard (FB-2-02):** a `Disconnect` that is not newer than the current accept is ignored.
  - **Telemetry:** correction percentiles and `stale_notices`; the host logs `timeout_notices`.
  - **FB-2-04 and FB-2-06:** labels and comments.
- **Tests:**
  - 3 fake-host integration tests; 2 of them failed before the guard.
  - 1 real-host smoke test, which does not discriminate (no late notice occurred).
  - 8 new analyzer self-checks (11 total).
  - Suites: unit 15, integration 25, network 12, all passing ×3 plus E2E.
  - Soak gate v2: lan, moderate and harsh at 120 s, 36/36 gates each (EXP-011).
- **Problems encountered:**
  - Analyzer v1 was shown to pass a run where client2 never started.
  - The old `soak.ps1` emptied the run directory, so it would have destroyed evidence on a repeat run.
  - The real-host notice test could not reproduce a late notice.
  - The host's `timeout_notices` was not logged during the v2 soak runs; it was added afterwards.
- **Paused work:** the reliable event channel is a WIP on local branch `feature/reliable-events` (commit `187a121`, not pushed). The transport choice now waits for the user (see below).
- **Remaining:** FB-2-05 stall coverage; harsh recovery variance (KI-18); shed gate (KI-19); transport decision.

## 2026-10-23: Real-time soak test (`feature/realtime-soak`, stacked on PR #1)

- **Changes:**
  - Host and client telemetry.
  - `--stall-at-s` / `--stall-s` client options.
  - `scripts/soak.ps1` with 3 profiles.
  - `tools/soak/analyze.py`, gates fixed before the first run, with 3 self-checks.
  - `docs/soak-testing.md`.
  - **KI-12 fix:** the host timeout notice, and the client reconnecting on it.
  - **KI-02 fix:** a high-resolution waitable timer in both executables.
  - TEST_RESULTS gains a real-time soak section.
- **Tests:**
  - 2 new integration tests: `timed_out_client_is_told_and_reconnects_fast` and `timeout_notice_is_rate_limited_and_scoped`.
  - 48/48 simulated-clock tests on 3 or more runs; E2E passed.
  - Soak: lan, moderate and harsh at 120 s each, all gates passed (EXP-010).
- **Problems encountered:** the first soak smoke run **failed** its recovery gate at 3085 ms against a 3000 ms limit (KI-12). It also showed about 50 ms of RTT overhead from sleep granularity (KI-02). Both were fixed and the failing run is preserved. The KI-12 tests were not written test-first (see review request #2 §5).
- **Remaining:** reliable event channel; desync digests; long soaks; the LAN test (needs approval).

## 2026-10-23: ChatGPT review #1 triage and fixes (`feature/ai-collaboration-bridge`)

- **Input:** [PR #1 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/1#issuecomment-6371152419), findings FB-1-01 through FB-1-07. The triage is in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md).
- **Changes:**
  - **Host step (FB-1-01):** exactly one timestep per tick, with backlog shedding above 2 queued inputs. Counted in `HostStats::shed_inputs`.
  - **Liveness (FB-1-02):** host and client refresh liveness only for messages valid in their direction.
  - **Evidence (FB-1-05):** `verification_scope` added to all findings and enforced by the validator.
  - **Reporting (FB-1-03):** simulated-clock and real-time results are labelled separately.
  - **Logs and E2E script:** logs are opened shareable (`cli::open_log`); `test.ps1` deletes stale E2E logs and does a warm-up run.
  - **Documentation:** protocol.md, decisions D-010, KNOWN_ISSUES.
- **Tests:** 8 new tests in total.
  - Integration (7): `input_burst_never_exceeds_one_timestep_per_tick`, `steady_inputs_are_never_shed`, `wrong_direction_messages_do_not_refresh_liveness`, `client_ignores_wrong_direction_messages_for_liveness`, `reconnect_token_migrates_slot_to_new_endpoint`, `reconnect_after_grace_expiry_gets_new_identity`, `client_survives_host_restart_with_new_session`.
  - Network (1): `jitter_never_exceeds_tick_movement_budget`.
  - These were written first. On the old code, 4 failed because of the real bugs; a 5th failed because of a test bug, since fixed.
- **Results:**
  - 46/46 passed (unit 15, integration 20, network 11) on 5 ctest runs plus 3 scripted runs.
  - E2E passed, including right after a relink (with the warm-up).
  - `test_report.py` 4/4. Evidence validator: 71 records, 0 errors.
- **Problems encountered:**
  - One new test was wrong; it was fixed, not the code.
  - `fopen_s` exclusivity made the host fail to open its log.
  - The old E2E wait polled a stale log.
  - EXP-006 was reproduced; its cause remains unknown.
  - A PowerShell 5.1 BOM was written into EXP-006.json and then removed.
- **Remaining:**
  - Real-time soak and telemetry (FB-1-03 P1).
  - Authentication and rate limiting before any WAN use (FB-1-02).
  - Reliable event channel (FB-1-04).
  - Timer fix, LAN test.

## 2026-10-23: ChatGPT collaboration bridge

- **Commits:** `e549d3c` (bridge) plus a follow-up docs commit, on `feature/ai-collaboration-bridge`.
- **Published:** the private repository https://github.com/M0nsT3rG/ProjectMordor and PR #1 https://github.com/M0nsT3rG/ProjectMordor/pull/1. The GitHub CLI was installed via winget with user authorisation, and the user signed in personally.
- **Changes:**
  - Added the `.ai-collaboration/` bridge files.
  - Added `tools/collab/report.py`, a local status and review-summary generator with no network access.
  - Added the collaboration rules to `CLAUDE.md`.
- **Tests:** ran `report.py` against the repository and checked its output against `git` and `evidence/logs`. The prototype suites were not re-run, because no C++ code changed.
- **Results:** see [TEST_RESULTS.md](TEST_RESULTS.md).
- **Problems:** the first device-flow login did not complete. On the second attempt the user entered the code manually.
- **Remaining:** wait for ChatGPT feedback on review request #1, then triage it in CHATGPT_FEEDBACK.md.

## 2026-10-23: Phase 1 (commit `7766667`, `main`)

- **Changes:**
  - Read-only game discovery and PE inventory.
  - Evidence store (62 findings, 8 experiments) with a validator.
  - Prior-art review.
  - Architecture, subsystem notes, feasibility matrix, risk register and decision docs.
  - Standalone UDP networking prototype with 38 tests.
  - Phase 1 report.
- **Tests:**
  - `ctest` (unit 15, integration 13, network 10), 5 or more consecutive passing runs.
  - `scripts/test.ps1 -E2E`, which runs one host and two clients at 40 ms latency, 20 ms jitter and 10% loss.
- **Results:** all passed. The LAN test is NOT_RUN (EXP-007).
- **Problems encountered and fixed:**
  - A `const char*` log field printed `true`.
  - A lost Disconnect packet.
  - A first-launch start delay flaked the E2E run (EXP-006).
  - Seven review defects (EXP-008): 16-bit id wrap, input-backlog ratchet, liveness not refreshed by retransmits, grace-slot hijack, stale ConnectAccept, and two weak tests.
- **Abandoned:** a static probe of the executable's sections (EXP-003), because it conflicts with the anti-circumvention rule.
- **Remaining:** see [CURRENT_TASK.md](CURRENT_TASK.md) and `docs/phase1-report.md` §10.
