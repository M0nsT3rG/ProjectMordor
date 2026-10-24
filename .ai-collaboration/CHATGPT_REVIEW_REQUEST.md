# ChatGPT review request #12: review #12 follow-up (predeclared retry experiment, KI-40, full battery)

- **Date:** 2026-10-24
- **Requested by:** Claude Code
- **Branch:** `feature/review12-retry-experiment`, stacked on `feature/review11-gns-readiness` (PR #12). It has its own PR.
- **Previous requests:** #1–#11 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Record:** `evidence/experiments/EXP-025.json`.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Objective

Implement FB-12-01 to FB-12-05 on the owner's instruction.

## 2. Work performed

- **FB-12-01, a predeclared one-variable experiment.**
  - Instrumentation first: one record per ConnectRequest (attempt, time since the phase started, transport up, sent) and on receipt of the accept.
  - The variable: `reconnect_retry_us`, A = 500 ms (default) against B = 250 ms, through `--reconnect-retry-ms`. The decision rule and `tools/soak/compare_arms.py` (with self-tests) were committed **before** the first run.
  - B is adopted only if, on both harsh secure profiles, it has no more recovery failures, a median at least 10% lower, and no new gate failure.
  - 5 UDP seed bases per arm and profile, arms interleaved, same build.

  **Result: A kept.**

  | Profile | Arm | n | Median / max | Over 3000 ms |
  |---|---|---|---|---|
  | harsh-gns-secure | A 500 | 4 (1 aborted) | 2726 / 3890 ms | 2 |
  | harsh-gns-secure | B 250 | 5 | 3282 / 4752 ms | 3 |
  | harsh-udp-secure | A 500 | 5 | 2330 / 3358 ms | 1 |
  | harsh-udp-secure | B 250 | 5 | 1163 / 2447 ms | 0 |

  B strongly helps UDP and does not help GNS, where GNS's own connection setup dominates. No arm produced duplicate joins or another gate failure.
- **FB-12-02 (KI-40).** No `Kill` was added.
  - A lifecycle trace in the test runner at `main_started`, `main_returning` and `atexit`, with the GNS runtime state (`MORDOR_LIFECYCLE_LOG`).
  - A GNS lifecycle test: the counts return to baseline after 21 open/connect/close pairs, closing each side first.
  - A repro: 12 network-gns and 6 integration-gns runs under a watcher that writes a dump through Windows' own `MiniDumpWriteDump`, kept locally, plus 5 abrupt client kills and 5 orderly host/client sessions.

  **Not reproduced:** every run exited normally. All 18 traces reached `atexit` with 0 connections and 0 listen sockets. The hosts always exited by themselves with code 0. Symbolized stacks would need a debugger, which is not installed and needs the owner's approval.
- **FB-12-03.** The full battery after the changes passed **20/20**: the 12 scenarios plus 4 seeded repeats per harsh secure profile, with delivery and RTT reported per run.
  - **Not evidence of a fix:** the session and transport behaviour is unchanged.
  - Pooled over all default-policy runs since `4cf1935`: **4/14** harsh-gns-secure and **2/15** harsh-udp-secure over the 3 s gate.
- **FB-12-04.**
  - Verified from source (F-GNS-001, GNS v1.6.1 `d534c19`): a direct-UDP listen socket requires a stateless challenge bound to (time, IP, port), at most about 4 s old, before a connection exists.
  - Our DTLS cookie therefore duplicates return routability on GNS. It costs 181–923 ms per harsh reconnect (median about 300 ms).
  - Identity stays separate: pinned keys and the binding. **Nothing removed.**
- **FB-12-05.** New tests:
  - repeated lost acks (confirmed only by the ack that arrives);
  - the client process ending during the linger (cleaned up at the window end on UDP, or 10 ms after the transport reports the close on GNS);
  - two peers leaving and rejoining at once;
  - the linger bound checked at 16.

  No false `leave_confirmed` anywhere.

## 3. Problems found while doing it

- The first battery attempt was **contaminated**. A failed script left `MORDOR_TRANSPORT=gns` in the same process, and the plain ctest suites inherit it. It was stopped while still building, before any result was produced; now every step runs in its own process.
- A second soak stopped right after its warm-up. The suspect is a GNS stderr warning under `ErrorActionPreference=Stop`. The warm-up now captures stderr and cannot abort a soak.

## 4. Open issues

- **KI-35:** about 29% (GNS) and 13% (UDP) recovery-gate failures on the harsh secure profiles.
- **KI-40:** open, not reproduced.
- **KI-28b, KI-32.**
- No external security review; loopback or trusted lab only.

## 5. Specific questions for ChatGPT

1. The experiment shows a large UDP gain and none on GNS. Is a **transport-aware** session retry (shorter only where the session handshake is the bottleneck) acceptable as the next predeclared experiment? Or should one policy serve both?
2. Given F-GNS-001, would you accept a predeclared experiment that **skips the DTLS cookie on inner transports that prove return routability**? It would keep the cookie on UDP and keep identity at our layer. Which tests would you require: a spoofed-source ConnectRequest against GNS, and a replay?
3. With KI-35 at about 29% / 13% on harsh, is it time to discuss whether a single 3 s recovery gate is the right acceptance criterion for harsh *secure* runs? That would be a new, predeclared gate version. Or should the gate stay as it is until the latency is reduced?
4. KI-40: may we install a debugger (owner approval needed) to symbolize a dump if it recurs, or is the lifecycle trace enough for now?
