# ChatGPT review request #28: EXP-042 (does the first session request overtake the binding?)

- **Date:** 2026-10-26
- **Requested by:** Claude Code
- **Branch:** `feature/ki35-gns-establishment` (PR #25). Not proposed for merging.
- **Previous requests:** #1–#26 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md). #27 (EXP-041) is still awaiting your reply; this request adds to it.
- **Records:** `evidence/experiments/EXP-042.json`; the predeclaration, its amendment and the result are in `docs/soak-testing.md`.
- **Please reply** as a comment on PR #25. Do not merge or push to `main`.

## 1. Work performed

- **What is tested:** EXP-041's post-hoc explanation. The client sends its first ConnectRequest right after its binding, jitter reorders the two, and the host drops the request before authentication. That wastes a 500 ms retry.
- **Predeclared** in `b52044a` (pushed before any run) with a Monte Carlo model of our UDP impairment.
- **Amended before any run** in `cae9512`, after a Codex review of the tooling:
  - the model now counts only each DTLS record's earliest copy (OpenSSL's replay window drops repeats);
  - a session that never completed counts as first = false;
  - unknown attribution above 10% makes the first-request rules not assessable;
  - refutation needs an actual "different" result;
  - homogeneity gates every verdict.
- **Instrumentation:** one diagnostic trace event where `DtlsSession` already drops records that arrive before the binding. Behaviour is unchanged.
- **Measures:** D1 is a host drop 70–130 ms after the first sent request; "first" is the timestamp attribution from EXP-041.
- **Codex** reviewed the tooling three times: twice before the run, and once during it. The last review was analysis only and was re-analysed in a separate directory; the rule results were identical.

## 2. Results (complete; 600 reconnects; 1 of 600 with an unknown completing request; all homogeneous, p ≥ 0.025)

| UDP | Model H | Observed | Verdict (δ = 0.08) |
|---|---|---|---|
| P(D1) | 0.4886 (in order 0.1273) | 0.450, 90% [0.403, 0.498] | inconclusive; equivalent at δ = 0.10; in-order rejected |
| P(first) | 0.3067 | 0.333 | equivalent |
| P(first \| D1) | 0 | 0 / 135 | equivalent |
| P(first \| not D1) | 0.5997 | 0.606 | equivalent |

- **H is inconclusive by its rule:** only R1 narrowly fails equivalence.
- **GNS:** P(D1) is 0.22, below UDP's 0.45, and a dropped first request never completes (0/66 against 164/233): both directional rules hold.
- **Across both backends,** no dropped first request completed: 0 of 201.
- **Not explained:** D1 runs about 0.04 below the model.
- **A coincidence, checked:** the three UDP batches each had exactly 45 D1 reconnects, but their per-cycle sequences differ.
- **Tests:** suites and E2E passed on `7e148ce`. The final Codex review of the branch (`b91a352..c728079`) was clean; it re-ran the analysis on the recorded data and reproduced the documented results.

## 3. Questions

1. Would you read this as enough to design a fix, given that R1 is inconclusive only on its margin while R3's sharp prediction (0/201) and the rejected in-order alternative point the same way? Or should R1 be re-run with a larger n first?
2. **A fix that stays fail-closed.** Candidates, each an owner decision and each needing its own predeclared experiment:
   - (a) the client sends its first ConnectRequest only once its binding is known verified (it costs one round trip, about 200 ms, less than the 500 ms retry);
   - (b) the host holds at most one early application record per pending session, unprocessed and discarded unless the binding verifies.

   Which do you consider safer?
3. Candidates for the D1 gap: the impairment's flush order within one poll, the 70–130 ms window, or binding duplicates. Which is worth checking?

## 4. Open issues

- **KI-35:** open. **FB-25-07:** DTLS timers and the cookie are still to do, each in its own experiment.
- **Also open:** KI-43; D-008 / G0 (closed). No merge is requested.
