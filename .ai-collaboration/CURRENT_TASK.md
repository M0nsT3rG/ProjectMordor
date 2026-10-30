# Current task

**Last completed:** review #23 follow-up and the first integration-gate run on `feature/review23-integration`. Record: EXP-035.
- 12 defects found by the combined review against `main` were fixed with regressions.
- Gate on `508ae40`: all green except KI-35 (harsh-gns-secure); Codex pass 6 pending; owner approval needed. **Nothing merged.**

**Before that:** review #22 follow-up on `feature/review22-escaping`. Record: EXP-034.
- JSON escaping in the runner; restoration manifest with digests; PRs #1–#21 closed and #22 consolidated onto `main`.

**Before that:** review #21 follow-up on `feature/review21-identity`. Record: EXP-033.
- FAILED cases fail the run; suite identity is checked; spaced and non-ASCII paths are redacted; lost GNS invocation evidence is restored.
- D-007 is recorded by the owner; **D-008 is pending, so G0 is still closed**.

**Before that:** review #20 follow-up on `feature/review20-validation`. Record: EXP-032.
- Failure reasons persisted; result files validated; commit and clean/dirty on recent runs.

**Before that:** review #19 follow-up on `feature/review19-freshness`. Record: EXP-031.
- Result freshness without timestamps; missing suites fail the run; recent-runs history in the report.

**Before that:** review #18 follow-up on `feature/review18-repeat`. Record: EXP-030.
- Every repeat invocation is kept with its exit code; the report shows run status.

**Before that:** review #17 follow-up on `feature/review17-evidence`. Record: EXP-029.
- Test evidence is immutable per run; overwritten evidence for 20 records recovered from git; the validator enforces it.

**Before that:** review #16 follow-up on `feature/review16-followup`. Record: EXP-028.
- Bounded diagnostics inside bounded waits; no comparator verdict from empty or non-finite cohorts; GNS shutdown on every exit path.

**Before that:** review #15 follow-up on `feature/review15-followup`. Record: EXP-027.
- KI-40 root-caused and fixed; KI-42 instrumented (not reproduced).
- The GNS-only cookie skip saved one RTT but was not adopted by the predeclared rule.
- At the head, the battery passed 20/20; pooled harsh-gns-secure 3/15 (KI-35).

**Before that:** reviews #13/#14 follow-up on `feature/review13-14-followup`. Record: EXP-026.
- Harness hardened (transport pinning, barrier, hang watcher); KI-41 contract explicit.
- The UDP retry gain did not replicate (500 ms kept).
- At the head, harsh-gns-secure still fails 5/13 (KI-35).

**Before that:** review #12 follow-up (FB-12-01 to FB-12-05) on `feature/review12-retry-experiment`. Record: EXP-025.
- The predeclared retry experiment kept 500 ms.
- The full battery passed 20/20, but the pooled harsh secure failure rate is about 29% (GNS) and 13% (UDP): KI-35 open.
- KI-40 not reproduced.

**Before that:** review #11 follow-up (FB-11-01 to FB-11-05) on `feature/review11-gns-readiness`. Record: EXP-024.
- DTLS waits for the inner GNS connection; departure edge cases hardened.
- v6 battery at HEAD 12/12, but the seeded harsh repeats still fail (KI-35); KI-40 found.

**Before that:** review #10 follow-up (FB-10-01 to FB-10-05, answer 5) on `feature/ki35-ki37-investigation`. Record: EXP-023.
- KI-37 and KI-30 resolved; protocol v3 (explicit leave); soak gate v6.
- KI-35 mechanisms shown and partly fixed; harsh-gns-secure still fails recovery (KI-35, KI-38). v6 soaks: 11/12.

**Previously:** review #9 follow-up (FB-9-01 to FB-9-05):
- protocol v2 application-level reliable events (KI-28 resolved, with limits);
- soak gate v5 (realized impairment);
- authenticated-only liveness;
- key-file hardening;
- handshake rate limiting.

v5 soaks passed 10 of 12: KI-35 and KI-37 are open. See EXP-022 and [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md).

**Branches and PRs.** On 2026-10-25 the owner closed PRs #1–#21 without merging. Each had been reviewed and its feedback triaged and implemented in the next PR. **PR #22 now targets `main` and carries the whole stack** for a future merge, which needs the owner. Follow-up PRs stack on #22. All branches are kept. The table is the history:

| PR | Branch | Base | Content |
|---|---|---|---|
| [#1](https://github.com/M0nsT3rG/ProjectMordor/pull/1) | `feature/ai-collaboration-bridge` | `main` | Phase 1 + collaboration bridge |
| [#2](https://github.com/M0nsT3rG/ProjectMordor/pull/2) | `feature/realtime-soak` | #1 | Real-time soak, gates v1–v3 |
| [#3](https://github.com/M0nsT3rG/ProjectMordor/pull/3) | `feature/gns-transport` | #2 | GNS spike (D-012) |
| [#4](https://github.com/M0nsT3rG/ProjectMordor/pull/4) | `feature/transport-interface` | #3 | Transport contract v2 + UDP backend |
| [#5](https://github.com/M0nsT3rG/ProjectMordor/pull/5) | `feature/gns-adapter` | #4 | GNS adapter, per-transport tests, gate v4, D-013 evaluation |
| [#6](https://github.com/M0nsT3rG/ProjectMordor/pull/6) | `feature/gns-hardening` | #5 | Review #5: counters, deadlines, impairment guard, GNS substitute tests, DTLS proof |
| [#7](https://github.com/M0nsT3rG/ProjectMordor/pull/7) | `feature/dtls-transport` | #6 | Review #6: DTLS over the Transport + binding, KI-27, asymmetric processes |
| [#8](https://github.com/M0nsT3rG/ProjectMordor/pull/8) | `feature/secure-transport` | #7 | Review #7: secure transport decorator, bounded pending state, timer policy, KI-27 reproducer |
| [#9](https://github.com/M0nsT3rG/ProjectMordor/pull/9) | `feature/secure-provisioning` | #8 | Review #8: secure executables, secure reliable channel, cookie-aware pending, idle timeout, KI-27 upstream |
| [#10](https://github.com/M0nsT3rG/ProjectMordor/pull/10) | `feature/secure-liveness` | #9 | Review #9: authenticated liveness, protocol v2 application events, soak gate v5, key-file hardening |
| [#11](https://github.com/M0nsT3rG/ProjectMordor/pull/11) | `feature/ki35-ki37-investigation` | #10 | Review #10: event-ack bounds, stateless cookie gate, protocol v3 leave handshake, KI-35 traces and fixes, soak gate v6 |
| [#12](https://github.com/M0nsT3rG/ProjectMordor/pull/12) | `feature/review11-gns-readiness` | #11 | Review #11: DTLS on inner Connected, departure edge cases, coalescing option, full battery with seeded repeats |
| [#13](https://github.com/M0nsT3rG/ProjectMordor/pull/13) | `feature/review12-retry-experiment` | #12 | Review #12: predeclared retry experiment (500 ms kept), KI-40 lifecycle trace and repro, GNS challenge finding, departure tests, battery 20/20 |
| [#14](https://github.com/M0nsT3rG/ProjectMordor/pull/14) | `fix/linger-eviction-rejoin` | #13 | PR #12 review P2: linger eviction must not close a rejoined player |
| [#15](https://github.com/M0nsT3rG/ProjectMordor/pull/15) | `feature/review13-14-followup` | #14 | Reviews #13/#14: harness hardening, KI-41 contract, E2E barrier, EXP-026, battery at the final head |
| [#16](https://github.com/M0nsT3rG/ProjectMordor/pull/16) | `feature/review15-followup` | #15 | Review #15: GNS cookie-skip A/B (not adopted), KI-40 root cause and fix, KI-42 tracing, bounded waits, battery 20/20 |
| [#17](https://github.com/M0nsT3rG/ProjectMordor/pull/17) | `feature/review16-followup` | #16 | Review #16: bounded diagnostics, comparator refuses empty/non-finite cohorts, GNS shutdown on every exit path |
| [#18](https://github.com/M0nsT3rG/ProjectMordor/pull/18) | `feature/review17-evidence` | #17 | Review #17: immutable per-run test evidence, recovery of overwritten evidence, validator rule |
| [#19](https://github.com/M0nsT3rG/ProjectMordor/pull/19) | `feature/review18-repeat` | #18 | Review #18: every repeat invocation kept with exit codes, run status in the report, budget and snapshot wording |
| [#20](https://github.com/M0nsT3rG/ProjectMordor/pull/20) | `feature/review19-freshness` | #19 | Review #19: results deleted before each invocation, missing suites fail the run, recent-runs history |
| [#21](https://github.com/M0nsT3rG/ProjectMordor/pull/21) | `feature/review20-validation` | #20 | Review #20: failure reasons in run.json, result-file validation, commit and dirty on recent runs |
| [#22](https://github.com/M0nsT3rG/ProjectMordor/pull/22) | `feature/review21-identity` | **`main`** (was #21) | Review #21: FAILED cases fail the run, suite identity, path redaction; lost GNS evidence restored; D-007 recorded. **Open: the consolidated PR.** |
| [#23](https://github.com/M0nsT3rG/ProjectMordor/pull/23) | `feature/review22-escaping` | #22 | Review #22: JSON escaping in the C++ runner, restoration manifest with digests, PR consolidation recorded |
| #24 | `feature/review23-integration` | #23 | Review #23: manifest v2, integration gate, 12 combined-review fixes; the integration candidate |

Local only: `feature/reliable-events` (in-house ReliableChannel WIP, not pushed).

**Repository:** https://github.com/M0nsT3rG/ProjectMordor (private)

## Waiting on the user

- **D-013:** the executables run over DTLS with provisioned keys (D-016). Still needed:
  - a stateless cookie (KI-30);
  - an independent review;
  - a LAN test. Until authentication is implemented and reviewed: loopback or trusted lab networks only.
- **Gate G0:** open since 2026-10-27 (D-007 and D-008: the GOG build). M1's criteria are drafted in `docs/m1-criteria.md` (per-session PASS/FAIL/INCONCLUSIVE/NOT RUN, eight stop conditions, reviews #30 and #32) with ETW network monitoring (§4a), a dry run on fixtures (EXP-045) and an ETW coverage experiment (EXP-046). Both PASSED: not elevated (11/11, 7/7 not covered) and elevated by the owner on `b500439` (11/11, 7/7 MATCH). ChatGPT's review #35 cleared the way for the tier A predeclaration: `docs/m1-criteria.md` §8, EXP-048 (NOT_RUN) and `tools/m1/tier_a.py`. Review #36 asked for changes before approval; they are made (outcome tests, privacy checks, a fixture-only rehearsal, `no_data_established`; review request #37), and review #37 found §8 ready for the owner, conditionally (self-checks on `cf03239` kept as evidence). Codex passes 7 and 8 found 4 and 1 defects, all fixed. **The owner approved §8 on 2026-10-27** (commit `6f2ed04`, code `e171fe0`). **Session 1 ran on 2026-10-28 (EXP-048 PARTIAL):** INCONCLUSIVE, with NO DATA not established (1203 lost ETW events and two unreadable files, KI-45). Every other gate held. The series is stopped and back with the owner. The owner asked for the fixes: a §8 amendment is written (explicit trace buffers, a Windows-1252 fallback, `gpu.nfo` as known binary), with EXP-050 (the buffers, measured elevated by the owner together with the dry run) and EXP-051 (the new series). Both are NOT_RUN. **The owner approved the amendment on 2026-10-28** (commit `fe0af56`, code `322f02d`), before ChatGPT's review of request #39. **EXP-050 PASSED elevated** (2026-10-28, the owner's runs on clean `f2ef9bd`: coverage 9/9 with 0 events lost under the flood and the mixed load, dry run 11/11). **EXP-051 session 1 ran the same day and left no evidence** (KI-45): the trace analysis ran out of memory and the owner ended it. EXP-051 is ABANDONED. **A second amendment** streams the analysis (only `netwatch.py`; Codex passes 14 and 15). **EXP-050 and the dry run passed elevated again on 2026-10-29** (the owner's runs on clean `7126ff0`, code `f1665ae`: coverage 9/9 MATCH with 0 events lost under the flood and the mixed load, dry run 11/11). **The owner approved it on 2026-10-29** for `a8f5d5a`. `tier_a.py` still labelled its record "EXP-051"; at the owner's choice the label now says "EXP-060", a session-tool change that **the owner approved as of `c4e9cce`**. **EXP-060 session 1 ran on 2026-10-30: FAIL**, only because the owner answered no to "closed through its menu?" (the owner reports a mistaken key). The network was clean (0 of 546803 events lost), the streamed analysis finished, and no position was observed. EXP-060 is FAILED and the series is stopped. **A third amendment**, at the owner's request, changes only `tier_a.py`: every yes/no typed in full, and the closing answers read back and confirmed (Codex pass 16: 1 defect, fixed). It awaits the owner's approval. The next series is EXP-061 (NOT_RUN). The next series is EXP-060 (EXP-052 to EXP-059 left to PR #25). Tier B is not approved. The second player's GOG copy also needs its read-only check.
- **LAN test:** approval for a temporary firewall rule.

## Next tasks (no user input needed)

1. Root-cause **KI-37** (lost final leave after a GNS reconnect) and **KI-35** (secure reconnect tail).
2. A cookie before any per-peer DTLS state (KI-29).
3. **KI-27:** follow ValveSoftware/GameNetworkingSockets#458.
4. **FB-2-05:** wider stall coverage; repeated soak runs to characterise recovery variance (KI-18).
