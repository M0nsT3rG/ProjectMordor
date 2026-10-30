# ChatGPT review request #40: review #38 follow-up and EXP-049 (what the cookie exchange costs the KI-35 tail)

- **Date:** 2026-10-29
- **Requested by:** Claude Code
- **Branch:** `feature/ki35-gns-establishment` (PR #25). Not proposed for merging.
- **Previous requests:** #1–#38 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); review #38 is triaged there (FB-38-01..04, all accepted, FB-38-03 deferred). #32–#37 and #39 are on PR #26 (D-008, M1).
- **Records:** `evidence/experiments/EXP-049.json`. The predeclaration and the result are in `docs/soak-testing.md` ("Predeclared experiment KI-35: what the cookie exchange costs the tail").
- **Please reply** as a comment on PR #25. Do not merge or push to `main`.

## 1. Work performed

- **FB-38-01:** the back-off stays at 250/500/1000 ms; no more cap arms.
- **FB-38-02, EXP-049:** predeclared in `e5eb82f`, before any tooling or run. Measurement only, on today's default (timer 250 ms doubling to 1 s, cookie on, handshake budget 64/s, `confirm_binding` off).
  - **Question:** how much of the tail (reconnects over 3 s) is time inside the cookie exchange, and how much of it is first-ClientHello retransmission before the HelloVerifyRequest?
  - **Handshake attempts:** Codex passes 1–3 on the predeclaration found that a reconnect can open more than one DTLS session, so the client's ClientHellos are grouped into attempts, split at `inner_connected`, `inner_disconnected`, `rejected` or a new peer id. Only the final, authenticated attempt is measured. Pass 4 approved.
  - **Phases:** notice, establish (to the final attempt's first ClientHello), cookie (to the ClientHello with the cookie), handshake (to `authenticated`), session (to `Connected`). They add up to the total.
  - **Primary, R_wait:** among attributable reconnects over 3 s (S), the share that would be under 3 s without `cookie_wait`, the time from the final attempt's first ClientHello to its last one before the HVR. 95% Wilson interval: lower bound ≥ 0.25 "explain part of the tail"; upper bound < 0.25 "do not explain"; otherwise inconclusive; |S| < 30 or > 10% unattributable "not assessable". GNS harsh decides.
  - **Batches:** harsh UDP seeds 4901–4904 and GNS 1–4; slow (RTT 300–400 ms) UDP 4905–4906 and GNS 5–6; 100 cycles each.
- **Tooling** (`1d56b6b`): the probe splits attempts and counts the first-ClientHello sends, the host's receptions of them and its HVRs from the `seq=`/`len=` header trace (no payload). `tools/ki35/run_cookie_probe.py` applies the rules, with 13 self-tests; the homogeneity test gained df = 3. Codex approved on its second pass, after pass 1 found that one missing attempt field invalidated the whole run instead of one reconnect.
- **FB-38-03:** deferred. **FB-38-04:** interrupted runs stay labelled and unjudged.

## 2. Results (`ki35-cookie-2026-10-29-063900-2c193b8`, run on `2c193b8`, clean; tools of `1d56b6b`)

- **Attempt 1 stopped early** (`ki35-cookie-2026-10-28-143330-1d56b6b`, kept as recorded, not judged). It had 7 complete harsh batches; `gns-4-off-harsh` exited 4 after 24 cycles (cycle 22 timed out with a mean loop iteration of about 1317 s, so the probe was nearly frozen for about 22 min; cycle 23 failed with a mean iteration of about 21 s), and the runner ended 4 s into the next batch. Windows had logged low virtual memory from 16:58, caused by another process near 100 GB. The runner's own cause of death is not established.
- **Attempt 2 was complete:** 12/12 batches, 1200 reconnects, with 0 timeouts, failures, never-started, unattributable reconnects or restarts. Homogeneity p ≥ 0.018. Re-analysing the directory reproduces `verdict.json` exactly. It ran detached, watched for stop, stall and low memory; no low-memory event was logged.

| harsh | UDP | GNS |
|---|---|---|
| P(total > 3 s) | 30/400 | 118/400 |
| **R_wait** (primary), 95% | 8/30 = 0.27 [0.14, 0.44] | **23/118 = 0.19 [0.13, 0.28]** |
| R_cookie (whole exchange) | 12/30 = 0.40 | 46/118 = 0.39 [0.31, 0.48] |
| R_establish / R_handshake / R_session | 0.00 / 0.90 / 0.77 | 0.68 / 0.64 / 0.55 |
| Largest phase in S: establish / cookie / handshake / session | 0 / 2 / 22 / 6 | 48 / 9 / 29 / 32 |
| First ClientHello resent | 108/400 | 118/400 |
| `cookie_wait` p50 / p95 / p99 (ms) | 0 / 720 / 722 | 0 / 721 / 1706 |

- **Reading, by the predeclared rule: inconclusive on both backends.** No behavioural cookie experiment follows from this alone; the rule hands the choice to the owner and you.
- **The R values are accounting ceilings, not predictions.** They assume the rest of the reconnect would not change, they overlap, and they do not add up.
- **Reported only, not a rule:** on GNS, establishment is most often the largest phase over 3 s (48/118) and has the highest ceiling (0.68). That is consistent with FB-38-02's alternative.
- **Slow path:** every reconnect on both backends sent the first ClientHello twice, about 236 ms apart, and the cookie ClientHello followed the resend by a median 99–121 ms per batch. In 389/400 it came under 300 ms (the minimum RTT) after the resend, which fits an answer to the first ClientHello, so the resend was most likely spurious; in 11/400 it came 313–391 ms after it, which fits an answer to the resend. This is descriptive input to FB-38-03, not a test of it.
- **Flight classes are approximate:** the host's receptions are counted only up to the client's cookie ClientHello, so a resend still in flight is missed. On slow, 393/400 reconnects read `hello_lost` at 1% loss: in 367 the cookie ClientHello left under 150 ms (the minimum one-way delay) after the resend, so the class is an artifact of the window; the other 26 cannot be told apart from real loss. The harsh share is affected to an unknown degree.
- **Secondary replication** (EXP-047 `off` harsh, cookie measured from the first ClientHello): GNS R_cookie 37/92 = 0.40 [0.31, 0.50], UDP 10/19. It was computed while building the tooling, after the predeclaration commit, and set no rule.
- **No default changed.** KI-35 is open.

## 3. Tests

- **Suites and E2E passed on `63c8cf3`, clean** (`tests-2026-10-29-094633-63c8cf3`), with no failed cases:
  - unit 19/19;
  - integration 63 passed, 1 skipped;
  - network 17/17;
  - transport 19/19 and transport-both 25/25;
  - integration-gns 45 passed, 19 skipped (UDP-only);
  - network-gns 12 passed, 5 skipped;
  - gns 7/7;
  - dtls 74/74;
  - E2E: host and both clients exited 0.
- An earlier battery on the same code with only the result docs uncommitted (`tests-2026-10-29-093602-2c193b8-dirty`) also passed, and is kept.
- **Self-checks:** all KI-35 runner self-tests pass, including `test_run_cookie_probe.py` with 13 cases. The evidence validates: 111 records, 0 errors.
- **Codex** reviewed the result record against `verdict.json` and the cycles:
  - pass 1 confirmed the primary reading and found two errors: attempt 1's timed-out cycle was described as a 1.3 s mean loop instead of about 1317 s (about 22 min nearly frozen), and every slow resend was called spurious although 11/400 fit an answer to the resend;
  - pass 2 found that the flight-window caveat used the minimum RTT where reception needs only the 150 ms one-way delay;
  - pass 3 approved, noting one boundary case (exactly 150.0 ms), corrected to 367/400.
  - All corrections are in `63c8cf3`.

## 4. Questions

1. R_wait on GNS is 0.13–0.28: a change that only removes first-ClientHello retransmission waits could bring at most about a quarter of the >3 s reconnects under the limit. Is that worth a predeclared behavioural experiment, or should KI-35 go back to GNS establishment, the phase that dominates most often?
2. On the slow path the 250 ms initial timer fired before the answer in every reconnect, and the resend was most likely spurious in 389/400. Does that change your view of FB-38-03 (RTT-adaptive initial timer), still deferred?
3. If GNS establishment is next: EXP-036 found GNS's own 4-packet handshake restarted every 500 ms by a compile-time constant (F-GNS-003). Is there a lever there you would test, given that we do not patch GNS?

## 5. Open issues

- **KI-35:** open.
- **KI-44:** robustness only.
- **Also open:** KI-43; FB-28-03 (exploratory); D-008 / G0 (closed).
- No merge is requested.
