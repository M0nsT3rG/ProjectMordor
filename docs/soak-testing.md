# Real-time soak testing

Review #1 (FB-1-03) pointed out that every automated test uses a simulated clock. The soak test covers the gap: the real executables run as separate processes on **wall-clock time** over loopback, under a fixed impairment profile.

## How to run

```powershell
./scripts/soak.ps1 -Profile lan       -DurationS 120
./scripts/soak.ps1 -Profile moderate  -DurationS 120
./scripts/soak.ps1 -Profile harsh     -DurationS 120
python -I tools/soak/analyze.py evidence/logs/soak-<date>-<profile>   # re-analyze a run
python -I tools/soak/test_analyze.py                                  # analyzer self-checks
```

Each run:

1. Warms up both binaries (the EXP-006 workaround).
2. Starts `mordor-host`.
3. Starts two clients: `client1` moves in a circle and `client2` moves on a line.
4. Freezes `client2` for 5 s at 40% of the duration. While frozen it sends, receives and simulates nothing, as if the process were suspended.
5. Writes `run.json`, `host.log`, `client*.log` and `summary.json` into `evidence/logs/soak-<date>-<profile>/`.

| Profile | Latency | Jitter | Loss | Duplication |
|---|---|---|---|---|
| lan | 2 ms | 2 ms | 1% | 0% |
| moderate | 40 ms | 20 ms | 5% | 2% |
| harsh | 80 ms | 40 ms | 15% | 5% |

The impairment is applied independently on **every sender** (host and both clients), each with its own seed.

## Gates

The gates were fixed in `tools/soak/analyze.py` **before the first run**. They must not be tuned to results.

| Gate | Rule |
|---|---|
| Exit codes | host and both clients exit 0 |
| Final state | each client's summary says `connected` |
| Invalid packets | 0 at the host and at each client |
| Tick drift | \|world ticks + skipped − uptime × rate\| / expected ≤ 1%; host skipped ticks = 0 |
| RTT p99 | ≤ 2 × (latency + jitter) + 60 ms scheduling allowance |
| Stalled client | exactly 1 reconnect; recovery from the end of the stall to `reconnected` ≤ 3000 ms |
| Other client | 0 reconnects |
| Host bookkeeping | timeouts = reconnects = number of stalled clients (a stall longer than the 3 s timeout must be recovered through the token, keeping the same identity); joins = number of clients |

Reported but not gated: RTT p50, p95 and max; corrections and the largest correction; stale snapshots; shed inputs; bandwidth in each direction.

## Results (2026-10-23)

| Run | Gates | RTT p50 / p99 (client1) | Recovery after stall | Shed inputs | Max correction | Drift | Bandwidth per client (in / out) |
|---|---|---|---|---|---|---|---|
| lan smoke, 20 s, **before fixes** | **FAILED** (recovery 3085 ms) | 55.7 / 66.8 ms | 3085 ms | 0 | 0 | 0.05% | — |
| lan smoke, 20 s, after fixes | passed | 9.8 / 12.2 ms | 19 ms | 0 | 0 | 0.003% | — |
| lan, 120 s | **passed** | 10.5 / 12.1 ms | 20 ms | 0 | 0 | 0.03% | ≈21 / 12–15 kbit/s |
| moderate, 120 s | **passed** | 104.2 / 120.8 ms | 161 ms | 2 | 0.33 | 0.001% | ≈21 / 23 kbit/s |
| harsh, 120 s | **passed** | 204.6 / 239.2 ms | 572 ms | 72 | 0.33 | 0.03% | ≈19 / 29 kbit/s |

Both failed and passing runs are kept under `evidence/logs/` (EXP-010).

## Gate version 2 (review #2, 2026-10-23)

Version 2 was fixed before its first run. It keeps every v1 gate and adds the following:

| Gate | Rule | Why |
|---|---|---|
| Manifest | `run.json` must list `expected_clients`. Client logs must match that list exactly: missing, renamed and extra logs all fail. Stalled clients must be in the manifest. Exit codes must cover the host and every expected client. Host joins must equal the manifest size. | FB-2-01: v1 passed a run where client2 never started |
| Coverage | Each client ran ≥ 95% of the duration. The host ran ≥ the duration. RTT samples ≥ 50% of 4/s × duration. | FB-2-03: explicit time coverage and sample minimums |
| Download ceiling | ≤ (snapshot 87 B × 30 Hz + ack 30 B × 4 Hz) × (1 + duplicate) × 1.05 | Upper bound derived from the protocol |
| Upload ceiling | ≤ (full 8-command input batch 119 B × 30 Hz + heartbeat 30 B × 4 Hz) × 1.05 | Upper bound derived from the protocol (offered load, before simulated loss) |
| Correction ceiling | max correction ≤ 5 u/s × (RTT bound + 3 ticks) | Reconciliation replays only unacknowledged inputs, which is at most one RTT of movement plus the host backlog |

The **RTT gate is unchanged** (configured + 60 ms) for comparability. It is a *synthetic loopback regression bound*, not a LAN criterion (FB-2-04). A tighter benchmark will be a new gate version after repeated runs.

**Evidence immutability:**
- Each run writes a fresh `evidence/logs/soak-<yyyy-MM-dd-HHmmss>-<profile>-g2/` directory.
- `soak.ps1` never deletes or reuses a directory.
- `analyze.py` refuses to overwrite an existing `summary.json`.
- v1 summaries keep `gate_version` absent.

| Run (gate v2, 120 s) | Gates | RTT p50 / p99 | Max correction (budget) | In / out kbit/s (budgets) | Recovery | Shed |
|---|---|---|---|---|---|---|
| lan | 36/36 | 10.5 / 12.1 ms | 0 (0.84) | 21.6 / 13.7 (22.9 / 31.0) | 18 ms | 0 |
| moderate | 36/36 | 103.9 / 121.6 ms | 0.33 (1.40) | 21.1 / 22.2 (23.4 / 31.0) | 161 ms | 8 |
| harsh | 36/36 | 203.9 / 241.6 ms | 0.33 (2.00) | 19.2 / 29.4 (24.1 / 31.0) | 1567 ms | 110 |

## Gate version 3 (review #2 follow-up, 2026-10-23)

Fixed before its first run. It keeps every v2 gate and adds:

- **Well-formed manifest.** Client names must match `^client[1-9][0-9]{0,2}$` and be unique. A duplicate can no longer be gated twice or inflate counts.
- **Scenario match.** `run.json` declares a `scenario`, which must be in the analyzer's fixed `SCENARIOS` table. The manifest and the stall plan must equal that scenario's. A self-declared list is no longer trusted.
- **Bandwidth gates are explicitly mean-rate gates** (`*_mean_budget`).
- **Burst reporting.** p95, p99 and peak of the 1 s window rates are reported (`window_rates_kbit_s`) but not gated.

| Run (gate v3, 120 s) | Gates | RTT p50 / p99 | Mean in / out kbit/s | 1 s peak in / out | Recovery | Shed |
|---|---|---|---|---|---|---|
| lan | 41/41 | 10.5 / 12.1 ms | 21.6 / 15.1 | 21.9 / 15.4 | 21 ms | 0 |
| moderate | 41/41 | 104.3 / 120.5 ms | 21.1 / 22.3 | 23.9 / 23.7 | 154 ms | 9 |
| harsh | 41/41 | 204.6 / 237.5 ms | 19.3 / 29.4 | 24.8 / 29.6 | 1328 ms | 107 |

**Observation.** The 1 s download peaks exceed the *mean* ceiling under moderate and harsh impairment (23.95 > 23.39 and 24.84 > 24.08 kbit/s). The mean ceiling must not be reused as a peak bound. A burst gate needs its own derivation (KI-20).

## Gate version 4 (GNS adapter, 2026-10-23)

Fixed before its first run. Numeric thresholds are unchanged from v3. New gates:

- `transport_declared`: `run.json` declares `transport` (`udp` or `gns`).
- `transport_matches_host_log`: the host's `host_stopped` line reports the same transport.

Results are recorded **per transport** and are not compared across transports: the impairment is applied differently (KI-25). Run with `./scripts/soak.ps1 -Profile <p> -Transport udp|gns`.

| Run (gate v4, 120 s) | Gates | RTT p50 / p99 | Mean in / out kbit/s | 1 s peak in / out | Recovery |
|---|---|---|---|---|---|
| lan, udp | 43/43 | 10.5 / 12.1 ms | 21.6 / 13.7 | 21.9 / 15.2 | 15 ms |
| moderate, udp | 43/43 | 103.4 / 121.8 ms | 21.1 / 21.6 | 23.9 / 24.3 | 153 ms |
| harsh, udp | 43/43 | 205.2 / 239.8 ms | 19.3 / 29.4 | 23.0 / 29.6 | 810 ms |
| lan, gns | 43/43 | 9.1 / 12.1 ms | 21.6 / 12.3 | 21.9 / 13.0 | 39 ms |
| moderate, gns | 43/43 | 95.8 / 115.4 ms | 20.8 / 21.2 | 22.5 / 22.1 | 355 ms |
| harsh, gns | 43/43 | 188.2 / 224.2 ms | 18.0 / 29.3 | 21.9 / 29.6 | 1239 ms |

Bandwidth is the session-layer payload rate, not GNS wire bytes (GNS adds its own headers and encryption).

**Re-run after review #5 (2026-10-24, EXP-017; connect deadline, impairment guard; gates unchanged):**

| Run | Gates | RTT p50 / p99 | Recovery |
|---|---|---|---|
| lan, udp | 43/43 | 10.4 / 12.1 ms | 18 ms |
| moderate, udp | 43/43 | 103.1 / 121.3 ms | 147 ms |
| harsh, udp | 43/43 | 204.4 / 239.7 ms | 826 ms |
| lan, gns | 43/43 | 9.0 / 12.1 ms | 30 ms |
| moderate, gns | 43/43 | 97.0 / 115.5 ms | 378 ms |
| harsh, gns | 43/43 | 188.6 / 228.5 ms | 783 ms |

**Failed v4 runs, preserved:**
- `soak-2026-10-23-225258-lan-gns-g4`: `host_timeouts_match_stalls` failed (3 timeouts, 1 expected). Clean leaves became timeouts: the unreliable `Disconnect` was lost when GNS closed. Fixed: `Disconnect` is sent reliably where supported, and `close()` drains reliable data.
- `soak-2026-10-23-230405-harsh-gns-g4`: `client1_rtt_p99` 313 > 300 ms. The first mapping gave the full jitter budget to both lag jitter and the reorder stage, exceeding the profile's worst-case delay. Fixed by splitting the budget; the gate was not changed.

## Gate v4 runs after review #8: plaintext and secure (2026-10-24, EXP-021)

Secure runs use `soak.ps1 -Secure`: all three processes run over DTLS with keys generated under `%TEMP%` and deleted afterwards. Gates are unchanged.

| Run | Gates | RTT p50 / p99 | Recovery |
|---|---|---|---|
| 042612-lan-udp | 43/43 | 10.1 / 12.1 ms | 20 ms |
| 042820-moderate-udp | 43/43 | 103.2 / 122.1 ms | 178 ms |
| 043028-harsh-udp | 43/43 | 205.5 / 239.1 ms | 813 ms |
| 043237-lan-gns | 43/43 | 9.1 / 12.1 ms | 32 ms |
| 043454-moderate-gns | 43/43 | 95.6 / 116.6 ms | 697 ms |
| 043703-harsh-gns | 43/43 | 189.4 / 234.8 ms | 773 ms |
| 043911-lan-udp-sec | 43/43 | 10.5 / 12.1 ms | 520 ms |
| 044120-moderate-udp-sec | 43/43 | 103.8 / 121.5 ms | 666 ms |
| 044328-harsh-udp-sec | 43/43 | 201.3 / 238.3 ms | 2797 ms |
| 044536-lan-gns-sec | 43/43 | 9.0 / 12.1 ms | 520 ms |
| 044744-moderate-gns-sec | 43/43 | 96.5 / 121.7 ms | 2195 ms |
| 044953-harsh-gns-sec | 43/43 | 188.5 / 230.4 ms | 1688 ms |

**Invalid for latency, preserved:** `soak-2026-10-24-034129/034337/034546-*-udp-sec-g4`. Their RTT p50 is 3 ms in moderate and harsh, because DTLS datagrams were sent with `now=0` and UDP's simulated latency and jitter therefore never applied (KI-34). The gates passed because they are upper bounds only.

**Close to its limit:** secure harsh on UDP recovered from the stall in 2797 ms, against the 3000 ms gate.

## Gate version 5 (review #9 FB-9-03, 2026-10-24)

Fixed before its first run. v1–v4 thresholds are unchanged. New gates:

- **Realized impairment.** v4 passed with UDP latency and jitter silently disabled (KI-34), so v5 checks that the configured impairment really happened:
  - `<client>_realized_rtt_floor`: min RTT ≥ 2 × latency − 2 ms;
  - `<client>_realized_handshake_delay`: first `connected` ≥ 1 RTT (plaintext) or 3 RTT (secure);
  - `realized_upstream_loss`: host `packets_in` / clients' `packets_out` within [1 − 2·loss − 0.02, 1 − loss/2 + dup].
- **Application-level events (FB-9-02)**, when `run.json` declares `event_interval_ms`:
  - `events_generated`;
  - `events_applied_all_but_in_flight` (≤ 4 per client at the end);
  - `events_dropped_only_at_end`;
  - `<client>_events_in_order`;
  - `<client>_events_applied`.

`soak.ps1` sends an event to every player every 500 ms by default. Directories are named `-g5`.

| Run (120 s) | Gates | RTT p50 / p99 | Recovery | Upstream ratio |
|---|---|---|---|---|
| 064505-lan-udp | 55/55 | 10.5 / 12.1 ms | 18 ms | 0.9903 |
| 064718-moderate-udp | 55/55 | 103.9 / 122.1 ms | 157 ms | 0.9699 |
| 064926-harsh-udp | 55/55 | 204.7 / 237.8 ms | 314 ms | 0.8928 |
| 065134-lan-gns | 55/55 | 9.0 / 12.1 ms | 34 ms | 0.9921 |
| 065342-moderate-gns | 55/55 | 96.0 / 115.5 ms | 389 ms | 0.9479 |
| 065551-harsh-gns | 53/55 **FAILED: host_timeouts_match_stalls, events_applied_all_but_in_flight** | 189.0 / 230.0 ms | 877 ms | 0.8573 |
| 065759-lan-udp-sec | 55/55 | 10.5 / 12.1 ms | 519 ms | 0.9902 |
| 070007-moderate-udp-sec | 55/55 | 103.6 / 120.5 ms | 671 ms | 0.9477 |
| 070215-harsh-udp-sec | 55/55 | 201.5 / 239.2 ms | 1810 ms | 0.8469 |
| 070424-lan-gns-sec | 55/55 | 9.1 / 12.1 ms | 519 ms | 0.9906 |
| 070632-moderate-gns-sec | 55/55 | 96.1 / 114.6 ms | 1703 ms | 0.9489 |
| 070840-harsh-gns-sec | 54/55 **FAILED: client2_recovery_time** | 191.5 / 234.3 ms | 3358 ms | 0.8477 |

**Failures, preserved:**
- **harsh, gns, plaintext.** The client that had reconnected after its stall lost its final clean leave, and the host timed it out (KI-37). The events gate failed as a consequence: the host kept queuing events for that player during its 6 s tail.
- **harsh, gns, secure.** Stall recovery 3358 ms, over the 3000 ms gate: the secure reconnect needs a new DTLS handshake (KI-35).

Quick 30 s runs used to validate v5 and to diagnose KI-35 are also kept (`soak-*-g5` with `duration_s` 30). Some of them fail `rtt_sample_count`, which is tighter in short runs.

## Gate version 6 (review #10 FB-10-05, 2026-10-24)

Fixed before its first run. v1–v5 thresholds are unchanged except where named here.

- **`<client>_realized_rtt_floor`** now uses the RTT 5th percentile, and needs at least 100 samples: p5 ≥ 2 × latency − 2 ms. The v5 minimum let one sample decide the gate. The minimum is still reported.
- **`<client>_realized_delivery`** (new): host `packets_in` for that player divided by the client's `packets_out`, inside the same bounds as the v5 aggregate (which is kept). An aggregate can hide one client with no loss and one with too much.
  - The host logs `player_stats` when a slot is released and at shutdown.
  - It counts session-valid packets only, so `ConnectRequest`s, and packets the stalled client sends before it learns it timed out, are missing. That is a few packets in thousands.
- **`<client>_leave_confirmed`** (new): the client's `leave` record must show a leave the host confirmed (protocol v3 `DisconnectAck`, KI-37).
- **Distributions** are reported in `summary.json` and not gated: per-client RTT p5/p50/p95/p99 and delivery ratios.

| Run (120 s) | Gates | RTT p50 / p99 (client1) | Recovery | Delivery ratio client1 / client2 |
|---|---|---|---|---|
| 094629-lan-udp | 59/59 | 10.1 / 12.2 ms | 20 ms | 0.9892 / 0.9804 |
| 094838-moderate-udp | 59/59 | 104.3 / 120.8 ms | 163 ms | 0.9681 / 0.9592 |
| 095046-harsh-udp | 59/59 | 205.7 / 240.3 ms | 1081 ms | 0.8916 / 0.8798 |
| 095254-lan-gns | 59/59 | 9.2 / 12.2 ms | 36 ms | 0.9900 / 0.9879 |
| 095504-moderate-gns | 59/59 | 96.1 / 117.7 ms | 381 ms | 0.9450 / 0.9454 |
| 095712-harsh-gns | 59/59 | 189.1 / 224.6 ms | 2280 ms | 0.8546 / 0.8410 |
| 095921-lan-udp-sec | 59/59 | 10.6 / 12.3 ms | 49 ms | 0.9887 / 0.9868 |
| 100129-moderate-udp-sec | 59/59 | 103.5 / 121.6 ms | 1247 ms | 0.9446 / 0.9415 |
| 100337-harsh-udp-sec | 59/59 | 203.9 / 239.1 ms | 1397 ms | 0.8478 / 0.8356 |
| 100546-lan-gns-sec | 59/59 | 9.1 / 12.3 ms | 64 ms | 0.9916 / 0.9864 |
| 100754-moderate-gns-sec | 59/59 | 96.0 / 118.7 ms | 930 ms | 0.9474 / 0.9470 |
| 101003-harsh-gns-sec | 58/59 **FAILED: client2_recovery_time** | 188.9 / 228.3 ms | 4168 ms | 0.8409 / 0.8369 |

- **harsh, gns, secure: failure preserved.** KI-35 on GNS. The trace: about 1 s waiting for the host's timeout notice, 2.5 s of cookie exchange and handshake (the first two ClientHellos never reached the host, KI-38), then 0.7 s of ConnectRequest retry.
- **harsh, gns, plaintext,** which failed in v5 (KI-37), passed, with every leave confirmed.
- **Known v6 limit (independent review), not changed because v6 has run:**
  - `realized_delivery` pairs a client's whole-run `packets_out` with the host's last `player_stats` for the player id the client ends with. A client that rejoined under a new id fails it, which is safe but misattributed.
  - The host also counts the client's leave `Disconnect`s, which are sent after the summary: a slight upward bias.
  - A v7 would fix both.
- Quick 30 s runs (`-g6`, `duration_s` 30) were used for the KI-35 traces. They fail `realized_rtt_floor` by design (fewer than 100 samples). `094607-lan-udp-g6` was aborted and has no summary; it is kept.

Directories are named `-g6`. Secure runs also log `dtls_trace` records (record types, epochs, handshake message types and milestones, never key material) for the KI-35 timeline (`tools/soak/timeline.py`).

## Gate v6 at the review #11 HEAD (2026-10-24, EXP-024)

The full 12-scenario battery, then 4 more seeded repeats of each harsh secure profile (FB-11-02). The gates are unchanged. UDP impairment seeds come from `-SeedBase` (host seed shown; the clients use +1 and +2). GNS fake loss is not seedable.

| Run (120 s, HEAD `4cf1935`) | Gates | RTT p50 / p99 (client1) | Recovery | Host seed |
|---|---|---|---|---|
| 172140-lan-udp | 59/59 | 10.4 / 13.3 ms | 18 ms | 101 |
| 172348-moderate-udp | 59/59 | 103.7 / 121.8 ms | 144 ms | 101 |
| 172557-harsh-udp | 59/59 | 203.9 / 239.0 ms | 770 ms | 101 |
| 172805-lan-gns | 59/59 | 9.0 / 11.5 ms | 34 ms | 101 |
| 173014-moderate-gns | 59/59 | 95.2 / 116.6 ms | 1149 ms | 101 |
| 173222-harsh-gns | 59/59 | 186.5 / 225.6 ms | 798 ms | 101 |
| 173431-lan-udp-sec | 59/59 | 10.5 / 13.7 ms | 56 ms | 101 |
| 173640-moderate-udp-sec | 59/59 | 104.7 / 122.7 ms | 974 ms | 101 |
| 173848-harsh-udp-sec | 59/59 | 202.2 / 238.5 ms | 2720 ms | 101 |
| 174057-lan-gns-sec | 59/59 | 9.0 / 12.6 ms | 66 ms | 101 |
| 174306-moderate-gns-sec | 59/59 | 95.5 / 113.9 ms | 1412 ms | 101 |
| 174514-harsh-gns-sec | 59/59 | 188.2 / 226.1 ms | 1572 ms | 101 |
| *repeats* | | | | |
| 174723-harsh-gns-sec | 58/59 **FAILED: client2_recovery_time** | 189.1 / 231.6 ms | 3056 ms | 111 |
| 174932-harsh-udp-sec | 59/59 | 201.1 / 240.8 ms | 2339 ms | 111 |
| 175141-harsh-gns-sec | 59/59 | 187.4 / 229.6 ms | 2900 ms | 121 |
| 175350-harsh-udp-sec | 59/59 | 204.1 / 240.8 ms | 2094 ms | 121 |
| 175559-harsh-gns-sec | 59/59 | 191.3 / 223.9 ms | 1808 ms | 131 |
| 175807-harsh-udp-sec | 59/59 | 204.3 / 238.9 ms | 1942 ms | 131 |
| 180016-harsh-gns-sec | **aborted by the harness**, no summary (kept) | | | 141 |
| 180018-harsh-udp-sec | 58/59 **FAILED: client2_recovery_time** | 204.4 / 239.1 ms | 3274 ms | 141 |
| 180329-harsh-gns-sec | 58/59 **FAILED: client2_recovery_time** | 188.6 / 222.8 ms | 5217 ms | 151 |

- **The 12-scenario battery: 12/12.** That includes harsh-gns-secure, which failed in the previous battery.
- **Repeats are not equivalent to that one pass:**
  - harsh-gns-secure fails **2 of 5** (1572, 3056, 2900, 1808, 5217 ms);
  - harsh-udp-secure fails **1 of 5** (2720, 2339, 2094, 1942, 3274 ms).

  KI-35 is open.
- **Where the time goes** (`tools/soak/timeline.py`):
  - On GNS, the host's timeout notice takes 174–467 ms. GNS then **establishes its own connection in 392–2072 ms** under the harsh profile. Only then does DTLS start (FB-11-01): 0 ClientHellos were sent early in every run.
  - Cookie exchange and handshake: 0.6–2.3 s with 0–4 DTLS timers.
  - ConnectRequest/Accept: 0.2–1.7 s, in multiples of the 500 ms reconnect retry.
  - The host never dropped a ClientHello because of a budget (`hello_rate_limited` 0 in every run). The admission changes are not the cause.
- **The earlier harsh-udp-secure runs** (1397 and 1467 ms) both had 0 DTLS timers. These runs have up to 3, so the spread is the loss realisation, not a code path that changed. That is a reading of the traces, not a proof.
- `180016-harsh-gns-sec-g6` stopped after the warm-up, before the host started; only warm-up logs remain. It is kept. The repeat was run again as `180329`.

## Predeclared experiment FB-12-01: session reconnect retry (2026-10-24, EXP-025)

Written and committed **before** its first run.

- **Question.** Does a shorter session-level reconnect retry reduce the harsh secure recovery tail? KI-35: the ConnectRequest/Accept phase took 0.2–1.7 s, in multiples of the 500 ms retry.
- **One variable:** the client's `reconnect_retry_us`.
  - A = 500 ms (the current default);
  - B = 250 ms (the same as the initial connect retry).

  Set with `soak.ps1 -ReconnectRetryMs`. Everything else is identical: same build, same gate v6, 120 s runs.
- **Runs:**
  - harsh-gns-secure and harsh-udp-secure;
  - UDP seeds `-SeedBase` 200, 210, 220, 230, 240, each run once with A and once with B;
  - A and B interleaved per seed, so drift affects both;
  - 20 runs. GNS fake loss is not seedable: its runs are paired by order only.
- **Measures, per arm and profile:**
  - client2 recovery: median, p95 (here the maximum of 5) and the count over 3000 ms;
  - session phase and ConnectRequests per reconnect (`timeline.py`);
  - realized delivery ratio and RTT p5/p50 as conditions;
  - any other gate failure.
- **Decision rule, fixed now.** B becomes the default only if, on **both** profiles:
  1. B has no more recovery failures than A;
  2. B's median recovery is at least 10% lower than A's;
  3. B introduces no gate failure other than `client2_recovery_time` that A does not have. Duplicate joins would show as `host_joins_match_manifest` and `host_reconnects_match_stalls`.

  Otherwise A stays the default and the result is reported as is. The 3000 ms gate is not changed either way.

## What the soak found, and what was fixed

1. **Slow recovery after a stall (KI-12, confirmed in real time).** When the client unfroze, it first read the snapshots that had piled up in its socket buffer. Those refreshed its liveness timer, so it believed it was still connected. The host had already timed it out and stopped sending, so the client noticed only after another full 3 s timeout.
   - **Fix:** the host sends `Disconnect{Timeout}` to an endpoint whose slot is in its grace period, at most once every 250 ms, and only to such endpoints, so arbitrary sources get no reply. On that notice the client reconnects immediately with its token.
   - **Result:** recovery went from 3085 ms to 19–572 ms depending on the profile.
2. **Real-time RTT overhead (KI-02).** `sleep_for(1ms)` slept about 15.6 ms. The impairment queue is flushed once per loop iteration, so the overhead stacked to about 50 ms of RTT.
   - **Fix:** a per-thread high-resolution waitable timer (`common/precise_sleep`), which changes no system-wide setting.
   - **Result:** lan RTT p50 dropped from 55.7 ms to 9.8 ms.

## Known limits of this test

- **Loopback only.** There is no real NIC, Wi-Fi or router. LAN between two PCs is still NOT_RUN.
- **Synthetic impairment.** The latency, jitter and loss are generated, not measured from a real network.
- **Short runs.** Each run is 120 s. Long runs (hours) and multiple stalls have not been tested.
- **RTT max includes the stall.** A heartbeat sent just before the stall and answered after it records a sample that includes the stall time (for example 5164 ms in harsh). That inflates RTT max but not p99.
- **The stall is simulated in-process.** The client loop sleeps; the OS process is not actually suspended. The network effect is the same, but scheduler effects are not modelled.
