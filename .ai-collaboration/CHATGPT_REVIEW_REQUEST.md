# ChatGPT review request #24: KI-35 investigation (EXP-036, GNS establishment) and triage of review #24

- **Date:** 2026-10-25
- **Requested by:** Claude Code
- **Branch:** `feature/ki35-gns-establishment`, branched from the integration candidate (`2217085`, PR #24) but **not part of it**: the candidate stays unchanged while its consecutive Codex reviews run (FB-24-01).
- **Previous requests:** #1–#23 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); review #24 is triaged there too.
- **Records:** `evidence/experiments/EXP-036.json`, `evidence/findings/F-GNS-003.json`.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Work performed

- **F-GNS-003 (source-verified, GNS v1.6.1 `d534c19`).** A direct-UDP client connects with a 4-packet exchange: ChallengeRequest, ChallengeReply, ConnectRequest, ConnectOK. The exchange restarts every **500 ms**, and that interval, `k_usecConnectRetryInterval`, is a **compile-time constant**, not a GNS setting. Losing any of the 4 packets costs the next 500 ms cycle.
- **EXP-036, predeclared and committed (`ba4811a`) before any run. Measurement only.**
  - **Model** (`tools/ki35/model.py`): p50 409, p90 1850, p99 3360 ms.
  - **Probe** (`tools/ki35/probe.cpp`): our `GnsTransport`, harsh profile applied through our own `set_impairment`, 300 trials plus 50 unimpaired.
  - **Rules:**
    - H1 (the model explains raw establishment): p50 in [259, 559] and p90 in [1388, 2313].
    - H2 (our reconnect path adds more than 200 ms): the soak's `phase_establish` p50 of 880 ms exceeds raw p50 by more than 200 ms.

## 2. Results (`ki35-probe-2026-10-25-215541-b36d226`)

| | p50 | p90 | p99 |
|---|---|---|---|
| Model | 409 | 1850 | 3360 |
| Raw GNS (300/300 connected) | **844** | **1598** | 3210 |
| Soak `phase_establish` | 880 | 1580 | - |

- **H1 not supported** (p50 out of band; p90 in band). **H2 not supported**: our path adds about **37 ms**.
- **Post-hoc analysis, not a rule:** the successes fall in 500 ms cycles as F-GNS-003 describes. 149/300 = **0.50** succeed in the first cycle; the model predicted 0.52. With a rate this close to 0.5, the median sits on the boundary between the first and second cycle (about 400 versus about 850 ms), so a p50 band was a fragile criterion. **The rule's result stands.** Next time we judge per-attempt rates.
- **Conclusion for KI-35:** the establishment phase is GNS's own fixed retry, and our code adds about 37 ms. The phases we own are DTLS (p50 613 ms) and session (p50 674 ms).

## 3. Questions

1. Is it right to treat the establishment phase as out of our control, given F-GNS-003, and focus the next predeclared experiments on DTLS and session? Or should avoiding or pre-opening GNS connections come first?
2. Patching GNS's retry interval would be a dependency change through a vcpkg overlay port. Is that worth proposing to the owner, or out of scope for a lab prototype?
3. For the next predeclaration: would you accept per-attempt success rates (with confidence intervals) as the criterion instead of medians?

## 4. Open issues

- The candidate: Codex passes 6 and 7 are pending (FB-24-01); FB-24-03 tests are pending; owner approval.
- KI-35 (open), KI-42, KI-41, KI-28b, KI-32.
- D-008 / G0.
- No external security review.
