# Current task

**Last completed:** review #18 follow-up on `feature/review18-repeat`. Record: EXP-030.
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

**Branches and PRs** (all open, none merged; merging needs the user):

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
| #19 | `feature/review18-repeat` | #18 | Review #18: every repeat invocation kept with exit codes, run status in the report, budget and snapshot wording |

Local only: `feature/reliable-events` (in-house ReliableChannel WIP, not pushed).

**Repository:** https://github.com/M0nsT3rG/ProjectMordor (private)

## Waiting on the user

- **D-013:** the executables run over DTLS with provisioned keys (D-016). Still needed:
  - a stateless cookie (KI-30);
  - an independent review;
  - a LAN test. Until authentication is implemented and reviewed: loopback or trusted lab networks only.
- **Gate G0:** D-007 (legal) and D-008 (target platform).
- **LAN test:** approval for a temporary firewall rule.

## Next tasks (no user input needed)

1. Root-cause **KI-37** (lost final leave after a GNS reconnect) and **KI-35** (secure reconnect tail).
2. A cookie before any per-peer DTLS state (KI-29).
3. **KI-27:** follow ValveSoftware/GameNetworkingSockets#458.
4. **FB-2-05:** wider stall coverage; repeated soak runs to characterise recovery variance (KI-18).
