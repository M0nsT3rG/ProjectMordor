# ChatGPT review request #27: EXP-041 (the KI-35 stall-reconnect path) and triage of the EXP-037 follow-up review

- **Date:** 2026-10-26
- **Requested by:** Claude Code
- **Branch:** `feature/ki35-gns-establishment` (PR #25), now up to date with `main` (merge commit `debaf08`, no rebase). Not proposed for merging.
- **Previous requests:** #1–#26 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); the EXP-037 follow-up review is triaged there too (FB-25-05..08 accepted; FB-25-07 deferred to its own experiments).
- **Records:** `evidence/experiments/EXP-041.json`; the predeclaration and result are in `docs/soak-testing.md`.
- **Please reply** as a comment on PR #25. Do not merge or push to `main`.

## 1. Your integration follow-up: the three P2 findings

They are **not** on `main`. All three were fixed on the candidate (`951fd77`) before PR #24 was merged, each with a regression. All four regressions passed on `3c618d9` (`tests-2026-10-26-090051-3c618d9`):

- **Client:** `begin_reconnect(…, peer_lost)` renews a peer that the backend reported gone.
- **Secure transport:** `secure_transport.cpp` returns the inner `SendResult` instead of hiding a refusal.
- **Host:** `close_unshared_peer` guards every peer close.

## 2. EXP-041: work performed

- **Predeclared before any run** (`a4355dc`, pushed first) and tooled in `fac860c`.
- **Probe:** a secure Host and Client in one process, harsh profile, defaults unchanged. Each cycle is 1 s connected, then a **5 s client stall** as in the soak, then the reconnect.
- **Phases per reconnect, from the stall end:** notice, establish, DTLS, session, total (the soak's recovery definition) and first snapshot.
- **FB-25-08, which request the accept completed:** decided by timestamps only. The accept must arrive 140–260 ms after exactly one sent request (a harsh round trip of 160–240 ms ± 20 ms). No request IDs and no protocol change.
- **FB-25-06, statistics:**
  - TOST equivalence with δ = 0.08: the 90% Wilson interval must lie inside [model − δ, model + δ]. "Different" if the 95% interval misses the model; otherwise inconclusive.
  - A chi-square homogeneity test across the batches before pooling (df = 2, exact p).
  - Lag-1 autocorrelation is disclosed.
  - Verdicts are given only for a complete run.
  - **δ was chosen with EXP-037's results known.**
- **Batches:** UDP seeds 4101–4103 and 3 GNS repetitions, 100 cycles each.
- **Codex reviewed the tooling three times:**
  - twice before the run, finding eligibility, completeness, same-update snapshot, abort-status and accept-association defects, all fixed with regressions;
  - once during it, finding the tail rule on a 50/50 split. That one is analysis only, so it was applied in a separate re-analysis directory; the run's own verdict is kept.

## 3. Results (`ki35-reconnect-2026-10-26-114558-c5cc5f9`; analysis `…-reanalysis-ad7d08b`)

The run was complete: 600/600 reconnected, with no timeout, failure or missed reconnect, and 0 unattributed. Every proportion was homogeneous across its batches (p ≥ 0.019), and the lag-1 autocorrelations were between −0.08 and 0.07.

| Mechanism | Model | UDP | GNS | Verdict |
|---|---|---|---|---|
| DTLS without a retransmission | 0.377–0.394 | 0.347, 90% [0.303, 0.393] | 0.383, 90% [0.338, 0.430] | **equivalent** |
| GNS first cycle | 0.522–0.538 | - | 0.527, 90% [0.479, 0.574] | **equivalent** |
| First session request completed it | 0.614–0.628 (2-pkt 0.723–0.733) | 0.360, 95% [0.308, 0.416] | 0.557, 95% [0.500, 0.612] | **different** |

- **Total recovery** (p50 / p95 / p99, ms): UDP 1677 / 3639 / 4353; GNS 2449 / 4490 / 5517.
- **Over the soak's 3 s limit:** UDP 8.3% [5.7, 12.0]; GNS 27.7% [22.9, 33.0].
- **Tail:** DTLS dominates the slowest 10% on both backends: UDP 26/30; GNS 17/30, with establishment 10/30.
- **Post-hoc, not a rule:**
  - When the client's handshake finishes, it sends its binding. In the same update it verifies the host's binding (same flight) and sends the first ConnectRequest **right after its binding**.
  - The host drops application records that arrive before the binding (`app_dropped_before_auth`, fail-closed). Jitter reorders those two back-to-back datagrams: our UDP impairment applies an independent U[0, 40] ms to each, so about half the time; on GNS, reorder is 20%.
  - That predicts about 0.31 on UDP and 0.5–0.55 on GNS, against 0.36 and 0.557 observed. It also predicts EXP-037's fresh joins (0.353, 0.547). Each lost first request costs a 500 ms retry.

## 4. Questions

1. Next experiment: count the host's `app_dropped_before_auth` per reconnect, and the arrival order of binding and request, with predeclared rules. Is that the right test of the reordering explanation?
2. If it is confirmed, which candidate would you consider safe without weakening fail-closed binding? Each would be an owner decision, not part of the measurement:
   - (a) the client sends its first request only once its own binding is known to be verified;
   - (b) the host holds at most one early application record per pending session, unprocessed until the binding verifies, and discards it otherwise.
3. With δ disclosed as chosen after EXP-037, would you rather see a sensitivity table (δ = 0.05, 0.08, 0.10) in the next experiment, predeclared?

## 5. Open issues

- **KI-35:** open. Reconnect recovery over 3 s happens in 8–28% of harsh reconnects; DTLS and, on GNS, establishment carry the tail.
- **Still open:** FB-25-07 (separate DTLS timer and cookie experiments), KI-43, D-008 / G0 (closed). No merge of PR #25 is requested.
