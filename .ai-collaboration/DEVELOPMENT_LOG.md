# Development log

Newest entries first. Commits are short hashes on the branch named in each entry.

## 2026-10-24: Review #10 follow-up (`feature/ki35-ki37-investigation`, stacked on #10)

- **Input:** [PR #10 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/10#issuecomment-6380695083), FB-10-01 to FB-10-05 and answer 5; the owner asked for all of it on this branch. Record: EXP-023.
- **Changes:**
  - FB-10-01: `EventAck` refused above the highest id the transport accepted (`event_acks_invalid`).
  - FB-10-02: an event counts as sent only on `Sent`; refused sends are retried with back-off.
  - Answer 5 / KI-30: stateless `DTLSv1_listen` cookie gate. Cookie-proven ClientHellos bypass the 64/s budget.
  - **Protocol v3:** `DisconnectAck`, client `Leaving` state (FB-10-04). The host keeps the transport peer of a cleanly-left player open for 1.5 s.
  - KI-35 instrumentation: `dtls_trace`, `clock` records, reconnect milestones, `player_stats`, transport stats; `tools/soak/timeline.py`.
  - KI-35 fixes from the traces: DTLS flight retransmissions coalesced into one datagram; ConnectRequest sent as soon as the transport is up.
  - Soak gate v6 (FB-10-05), fixed before its runs.
- **Tests:**
  - unit {'PASSED': 17}; integration {'PASSED': 44}; network {'PASSED': 17}; transport {'PASSED': 18}; transport-both {'PASSED': 20}; integration-gns {'PASSED': 30, 'SKIPPED': 14}; network-gns {'SKIPPED': 5, 'PASSED': 12}; gns {'PASSED': 7}; dtls {'PASSED': 50};
  - ×2 on both builds, plus E2E;
  - asymmetric 4/4; secure E2E 4/4; KI-27 reproducer 0 assertions with the 50 ms settle (10/10);
  - **v6 soaks 11/12** (harsh-gns-secure failed recovery, 4168 ms).
- **Problems encountered:**
  - the FB-8-01 test caught forged ClientHellos starving a real reconnect through the global rate limit (fixed);
  - one soak was aborted when the battery was restarted outside the tool time limit (kept);
  - the GNS case of the flight-coalescing test cannot be reproduced with process-global impairment (NOT RUN).
- **Remaining:** KI-35 on GNS (KI-38 hypothesis), KI-28b, LAN test, independent review.

## 2026-10-24: Review #9 follow-up (`feature/secure-liveness`, stacked on #9)

- **Input:** [PR #9 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/9#issuecomment-6379227966), FB-9-01 to FB-9-05; the owner asked for everything, including FB-9-02.
- **Changes:**
  - Authenticated-only liveness.
  - Handle-based key loading; temp-then-move write.
  - Misuse tests; ClientHello rate limit.
  - **Protocol v2** application events (D-017) in Host/Client and the executables.
  - Soak gate v5 (realized impairment and events).
  - DTLS retransmit cap 1 s.
  - Diagnostics: handshake time and retransmits in `secure_stats`.
- **Tests:**
  - unit {'PASSED': 17}; integration {'PASSED': 40}; network {'PASSED': 17}; transport {'PASSED': 18}; transport-both {'PASSED': 20}; integration-gns {'PASSED': 26, 'SKIPPED': 14}; network-gns {'SKIPPED': 5, 'PASSED': 12}; gns {'PASSED': 7}; dtls {'PASSED': 48};
  - ×2, plus E2E;
  - asymmetric 4/4; secure E2E 2/2;
  - **v5 soaks 10/12**.
- **Problems encountered:**
  - three defects found and fixed (rate-limit starvation, head-of-line `Disconnect`, events stalled behind a discarded first event);
  - KI-35 and KI-37 open;
  - symlink check not run.
- **Remaining:** KI-35, KI-37, KI-30, LAN test, independent review.

## 2026-10-24: Review #8 follow-up (`feature/secure-provisioning`, stacked on #8)

- **Input:** [PR #8 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/8#issuecomment-6377503056), FB-8-01 to FB-8-05, on the owner's go-ahead ("faz tudo"). Also: the KI-27 upstream draft was refined, approved and posted as ValveSoftware/GameNetworkingSockets#458 (EXP-020).
- **Changes:**
  - Cookie-aware pending replacement and a reserved reconnect slot.
  - Secure reliable channel (in-flight cap 32).
  - Idle timeout with keepalives.
  - `mordor-keygen`, DPAPI key files with a user-only ACL, public invitations, fail-closed `--secure-*`.
  - `scripts/secure-e2e.ps1`; `soak.ps1 -Secure`.
  - Fixes: GNS pre-close message delivery, GNS callback drain, decorator delivery before disconnect, DTLS time propagation.
- **Tests:**
  - unit {'PASSED': 16}; integration {'PASSED': 34}; network {'PASSED': 16}; transport {'PASSED': 18}; transport-both {'PASSED': 20}; integration-gns {'PASSED': 20, 'SKIPPED': 14}; network-gns {'SKIPPED': 5, 'PASSED': 11}; gns {'PASSED': 7}; dtls {'PASSED': 42};
  - soak v4 43/43 × 12 (6 plaintext, 6 secure);
  - secure E2E 2/2; asymmetric 4/4;
  - KI-27 stress 0.
- **Problems encountered:**
  - five defects (EXP-021);
  - three secure UDP soaks invalid for latency (kept).
- **Remaining:** KI-30, KI-28, KI-32, LAN test, independent review.

## 2026-10-24: Review #7 follow-up (`feature/secure-transport`, stacked on #7)

- **Input:** [PR #7 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/7#issuecomment-6376769358), FB-7-01 to FB-7-05, on the owner's go-ahead; also prepare the KI-27 upstream draft.
- **Changes:**
  - `SecureTransport` decorator (D-015): Host/Client over DTLS, no plaintext fallback.
  - Caps and expiry for pending sessions.
  - `DTLS_set_timer_cb` back-off.
  - Binding reply rate limit; multi-pin.
  - KI-27 back to open; minimal reproducer `tools/ki27-repro`; upstream draft `docs/upstream/gns-ki27-issue.md` (not published).
- **Tests:**
  - unit {'PASSED': 16}; integration {'PASSED': 34}; network {'PASSED': 16}; transport {'PASSED': 17}; transport-both {'PASSED': 19}; integration-gns {'PASSED': 20, 'SKIPPED': 14}; network-gns {'SKIPPED': 5, 'PASSED': 11}; gns {'PASSED': 7}; dtls {'PASSED': 30};
  - ×2, plus E2E;
  - soak v4 43/43 × 6;
  - asymmetric 4/4;
  - KI-27 stress 0/10 processes.
- **Problems encountered:** with the default OpenSSL back-off, 2 DTLS tests failed (one intermittently); fixed with the timer policy.
- **Remaining:** KI-30 (stateless cookie), KI-31 (executable provisioning), independent review, owner decision on the upstream post.

## 2026-10-24: Review #6 follow-up (`feature/dtls-transport`, stacked on #6)

- **Input:** [PR #6 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/6#issuecomment-6376091101), FB-6-01 to FB-6-05, on the owner's go-ahead.
- **Changes:**
  - `DtlsSession` over `Transport::Unreliable` with a fail-closed invitation binding, HVR cookies and a pending session for same-address reconnects.
  - KI-27 trigger localised and empirically mitigated (still open, FB-7-03): process-lifetime GNS runtime plus 50 ms settle; assertion counter and regression gates.
  - `scripts/asymmetric.ps1` with `tools/e2e/check_asymmetric.py`.
  - `--connect-deadline-s` bounded to [1, 120] s.
  - Wording sweep.
- **Tests:**
  - unit {'PASSED': 16}; integration {'PASSED': 34}; network {'PASSED': 16}; transport {'PASSED': 17}; transport-both {'PASSED': 19}; integration-gns {'PASSED': 20, 'SKIPPED': 14}; network-gns {'SKIPPED': 5, 'PASSED': 11}; gns {'PASSED': 7}; dtls {'PASSED': 17};
  - ×2 in both builds, plus E2E;
  - soak v4 43/43 × 6;
  - asymmetric 4/4.
- **Problems encountered:**
  - DTLS transport first run 15/16;
  - a weak first test draft;
  - KI-27 probe numbers exist only as console transcriptions.
- **Remaining:** game traffic over DTLS, a stateless cookie, an independent review (KI-29).

## 2026-10-24: Review #5 follow-up (`feature/gns-hardening`, stacked on #5)

- **Input:** [PR #5 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/5#issuecomment-6375464043), FB-5-01 to FB-5-05; the owner approved the implementation and the DTLS proof test.
- **Changes:**
  - Counter semantics fixed and documented (wire-level diagnostics).
  - `connect_deadline_us` at the session layer.
  - GNS impairment single-config guard (`set_impairment` returns bool; `impairment_refused`).
  - 8 `_over_transport` substitute tests.
  - `docs/test-coverage.md`.
  - `tests/dtls` proof (OpenSSL is now a direct dependency of the GNS build).
- **Tests:**
  - UDP: unit 15, integration 34, network 16, transport 17.
  - GNS: transport 17, integration 20 P / 14 SKIP, network 11 P / 5 SKIP, spike 7, dtls 8.
  - ×2, plus E2E. Soak v4 43/43 × 6.
- **Problems encountered:**
  - a false GNS pass exposed by the guard;
  - the DTLS first run failed (DANE);
  - two substitute drafts failed on GNS (the old connection is really closed);
  - a pre-existing GNS teardown assertion (KI-27).
- **Remaining:** DTLS integration and an independent review (KI-29); application-level delivery (KI-28).

## 2026-10-23: GameNetworkingSockets adapter (`feature/gns-adapter`, stacked on #4)

- **Input:** ChatGPT recommendation after review #4 (priorities 1–3).
- **Changes:**
  - `GnsTransport` behind contract v2 (`src/network/gns_transport.*`); capabilities extended with `exact_loss_accounting` and `per_instance_impairment`; new counter `closed_with_unacked_reliable`.
  - Client: transport `Disconnected` handled in every state; reconnect opens a new connection; D-014 (the transport owns establishment). Clean `Disconnect`/`HostShutdown` sent reliably where supported.
  - `--transport udp|gns` on host and client; `MORDOR_TRANSPORT` for tests; `UDP_ONLY(reason)` → SKIPPED.
  - 5 new reliable-backend contract tests; a symmetric-outage integration test.
  - Soak gate v4 (transport declared and matched); `soak.ps1 -Transport`; `test.ps1 -GnsBuild`; `report.py` exact suite matching and a Skipped column.
  - `docs/d013-evaluation.md`.
- **Tests:** first GNS runs failed (contract 7/10, integration 9 P / 3 F, network 6 P / 3 F), preserved as `*-first-run-*` logs. Final: UDP unit 15, integration 26, network 12, transport 15; GNS transport 15, integration 12 P / 14 SKIP, network 8 P / 4 SKIP, spike 7; ×2, plus E2E. Soak v4 43/43 on 3 profiles × 2 transports.
- **Problems encountered:** 8 defects (EXP-016), two failed soaks preserved (lan timeouts, harsh p99 313 ms).
- **Remaining:** D-013 owner decision; asymmetric GNS coverage (KI-24).

## 2026-10-23: Review #4 triage, Transport contract v2 (`feature/transport-interface`)

- **Input:** [PR #4 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/4#issuecomment-6373680146), FB-4-01 to FB-4-05.
- **Changes:**
  - Contract v2: `PeerId` + generation, lifecycle events, delivery classes, `SendResult`, failure counters, `flush()`.
  - `UdpTransport` v2 (`udp_transport.cpp`).
  - Host and client migrated to `PeerId` and events; transport disconnects are handled; send errors are counted.
  - `ImpairedSender` reports per-send outcomes and delayed failures.
  - New `tests/transport` contract suite.
  - Auth state machine §4a; D-013 options revised.
- **Tests:**
  - UDP suites and harness unchanged, ×3 in both builds.
  - Contract 10/10, and the mutation check failed exactly the 2 targeted tests.
  - E2E passed; moderate soak v3 41/41 (EXP-015).
- **Problems encountered:** a draft double poll in `Host::update`, caught before compiling.
- **Remaining:** GNS adapter (must pass `tests/transport` unchanged); D-013.

## 2026-10-23: Review #3 triage, Transport interface (`feature/transport-interface`, stacked on #3)

- **Input:** [PR #3 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/3#issuecomment-6373375242), findings FB-3-01 to FB-3-06.
- **Changes:**
  - `net::Transport` + `UdpTransport` + factory; Host and Client run on the interface.
  - `ImpairedSender` takes any transmit function.
  - GNS auth facts verified in the library's headers and source.
  - `docs/authentication-design.md`.
  - GNS tests rewritten: RAII state guard, port retries, concurrent, multi-client, mixed, queue pressure, asserted security baselines.
  - D-012 updated; D-013 pending.
- **Tests:**
  - No UDP test or harness file changed by the refactor; only `tests/gns/` was rewritten, deliberately.
  - Unit, integration and network 52/52 ×3 in both builds; E2E passed; lan soak v3 41/41, metrics unchanged.
  - GNS 7/7 ×3, and 21/21 across 3 parallel instances (EXP-014).
- **Problems encountered:**
  - Git checkout converted files to CRLF, which broke a PowerShell exact-match edit script. Nothing was written, because the script aborts on a missing anchor; I switched to the Edit tool.
  - FB-3-06 changed the planned impairment layering for GNS.
- **Remaining:**
  - GNS adapter (KI-22).
  - D-013 crypto library, then the authenticated-peer implementation and its tests (KI-21).

## 2026-10-23: Review #2 follow-up, soak gate v3 (`feature/realtime-soak`)

- **Input:** [PR #2 comment 6372799837](https://github.com/M0nsT3rG/ProjectMordor/pull/2#issuecomment-6372799837).
- **Changes:**
  - Gate v3: valid and unique manifest names; a known scenario; manifest and stall plan must equal the scenario.
  - Bandwidth gates relabelled as mean-rate.
  - 1 s window peak/p95/p99 reporting; client status lines carry cumulative bytes.
  - `soak.ps1` writes `scenario` and `-g3` run directories.
- **Tests:**
  - Analyzer self-checks 17/17 (6 new).
  - Suites 52/52 ×3 (built in a separate git worktree while GNS compiled).
  - v3 soak: lan, moderate and harsh at 120 s, 41/41 gates each (EXP-012).
- **Problems encountered:**
  - The new self-checks found that a non-string client name crashed the analyzer. Fixed.
  - Burst peaks exceed the mean ceiling (KI-20).

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
