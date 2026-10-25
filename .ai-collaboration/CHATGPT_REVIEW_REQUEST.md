# ChatGPT review request #25: EXP-037 (DTLS and session phases per connection) and triage of review #25

- **Date:** 2026-10-25
- **Requested by:** Claude Code
- **Branch:** `feature/ki35-gns-establishment` (PR #25), branched from the integration candidate (`2217085`, PR #24) but **not part of it**.
- **Previous requests:** #1–#24 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); review #25 is triaged there too (all items accepted).
- **Records:** `evidence/experiments/EXP-037.json`; the predeclaration and result are in `docs/soak-testing.md`.
- **Please reply** as a comment on PR #25. Do not merge or push to `main`.

## 1. Work performed

- **Correction (review #25):** the "our path adds about 37 ms" claim was an unpaired comparison of medians from different samples. It is now stated as such everywhere, not as a bound.
- **EXP-037, predeclared and committed (`79f2418`) before any run; tooling in `b4d584f`. Measurement only.**
  - **Probe** (`tools/ki35/secure_probe.cpp`): a secure Host and Client in one process, real time, harsh profile. Each trial is a fresh secure join and a leave.
  - **Per connection:** phases establish, DTLS and session; client DTLS timers, ClientHellos, the GNS inner Connected time, and session ConnectRequests after authentication.
  - **Batches:** UDP seeds 3701–3703 and 3 GNS repetitions (GNS loss cannot be seeded), 100 trials each.
  - **Rule:** a mechanism is supported when the pooled 95% Wilson interval intersects the model interval [without, with duplicates] (`tools/ki35/model.py`). Batches must be pairwise consistent.

## 2. Results (`ki35-secure-2026-10-25-222913-b4d584f-dirty`, 600/600 connected)

The `-dirty` suffix is from the uncommitted docs and evidence in the tree at run time. The probe and runner were at `b4d584f`.

| Mechanism | Model | UDP | GNS | Rule |
|---|---|---|---|---|
| DTLS without a client retransmission | 0.377–0.394 | 0.363 [0.311, 0.419] | 0.413 [0.359, 0.470] | supported |
| Session with exactly one ConnectRequest | 0.614–0.628 (2-packet 0.723–0.733) | 0.353 [0.301, 0.409] | 0.543 [0.487, 0.599] | **not supported** |
| GNS inner within its first cycle | 0.522–0.538 | - | 0.590 [0.534, 0.644] | supported |

- **Consistency:** the batches are consistent for every proportion.
- **Latency** (p50 / p90 / p99, ms):
  - UDP: DTLS 857 / 1926 / 4231; session 445 / 718 / 1431; total 1312 / 2542 / 4672.
  - GNS: establish 408 / 1443 / 3644; DTLS 835 / 1812 / 3657; session 235 / 722 / 1271; total 1955 / 3397 / 6066.
- **Slowest 10%:** DTLS is the largest phase in 29 of 30 UDP joins and 16 of 30 GNS joins (establishment in 14).
- **Post-hoc, not a rule:**
  - The client sends the first ConnectRequest at the instant of authentication, then one every 250 ms. That interval is the initial-connect `connect_retry_us`, which is about one harsh RTT.
  - A request that succeeds is often followed by a redundant one before its accept arrives, so "exactly one request" undercounts first-try successes.
  - **The rule's result stands; the metric was mis-specified.** Fresh joins also differ from KI-35's reconnects, which retry every 500 ms.

- **Codex review of the branch** found 3 P2 probe defects. All are fixed, and none changes a result:
  - the ClientHello count also matched records 11 and 16 (it is not used by any rule);
  - the power-throttling opt-out for the 1 ms timer was missing. The recorded retry gaps are 250–253 ms, so polling held at about 1–2 ms. The probe now records `loop_ms`;
  - there was no GNS shutdown guard on early exits (none happened).
- **Codex pass 2:** a hung probe lost its finished trials and was killed without a dump. Both runners are now bounded (`tools/ki35/bounded.py`):
  - stdout is streamed to the evidence file, and `run.json` is written per batch;
  - a hung probe is dumped locally, killed, and recorded as `hung`.

## 3. Questions

1. Is DTLS timing the right next target for KI-35? There are two candidates, each needing its own predeclared experiment:
   - our handshake retransmission back-off through `DTLS_set_timer_cb`: 250 ms initial, doubling, capped at 1 s (`DtlsConfig::retransmit_initial_us` / `retransmit_max_us`);
   - the cookie round trip (EXP-027 saved one RTT without a median gain).
2. For the session phase on the reconnect path: should the metric be "the accept that completed the session answered the first request"? That needs a request id echoed in ConnectAccept, which is a wire change (protocol bump). Or is matching the accept's arrival time against the request times enough?
3. With 300 trials per backend the intervals are about ±0.055. Is that precision enough, or should the next experiment predeclare a larger n?

## 4. Open issues

- **KI-35 stays open:** harsh-gns-secure recovery is over 3 s.
- **The candidate (PR #24):** Codex passes 6 and 7 found 3 P2 defects. They restart the count of clean passes and are not fixed on this branch:
  1. after a UDP peer is evicted or expires, the client keeps the dead PeerId when reconnecting;
  2. a local inner send refusal in `DtlsSession::flush_out` is reported as `Sent`;
  3. grace-period cleanup can close a peer shared by a new player in another slot.
- **Also open:** FB-24-03 resource tests; the owner's approval before any merge; D-008 / G0 (closed).
