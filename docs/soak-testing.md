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

**Result (`tools/soak/compare_arms.py`, build `849a745`): the rule keeps A. B is not adopted.**

| Profile | Arm | n | Recovery median / max | Over 3000 ms | Runs (ms) |
|---|---|---|---|---|---|
| harsh-gns-secure | A 500 ms | 4 (1 aborted) | 2726 / 3890 ms | 2 | 1822, 1843, 3609, 3890 |
| harsh-gns-secure | B 250 ms | 5 | 3282 / 4752 ms | 3 | 1558, 2347, 3282, 3895, 4752 |
| harsh-udp-secure | A 500 ms | 5 | 2330 / 3358 ms | 1 | 1648, 1923, 2330, 2676, 3358 |
| harsh-udp-secure | B 250 ms | 5 | 1163 / 2447 ms | 0 | 945, 952, 1163, 1658, 2447 |

- **UDP:** B met every condition. The median halved and there were no failures. The session phase (ConnectRequest/Accept after a loss) is a real lever there.
- **GNS:** B did not. The difference is within the run-to-run spread; there the tail is dominated by GNS's own connection setup and DTLS losses, which the session retry cannot touch.
- The rule needs both profiles, so A (500 ms) stays the default, as predeclared. A transport-specific retry would be a new experiment, declared before its runs.
- The realized conditions were comparable across arms: client2 delivery 0.83–0.85, RTT p50 188–190 ms (GNS) and 202–206 ms (UDP).
- No arm introduced another gate failure: no duplicate joins, and the reconnect counts matched.
- `192116-harsh-gns-sec-g6` (arm A, seed 230) stopped after its warm-up, the second such case. It is kept. `soak.ps1` now runs warm-ups with stderr captured and non-fatal.

## Gate v6 after review #12 (2026-10-24, EXP-025)

The full battery plus 4 seeded repeats per harsh secure profile, on `2f2fe20`. Defaults are unchanged (reconnect retry 500 ms), and so are the gates.

| Run (120 s) | Gates | RTT p50 / p99 (client1) | Recovery | client2 delivery | Host seed |
|---|---|---|---|---|---|
| 200036-lan-udp | 59/59 | 10.1 / 12.6 ms | 20 ms | 0.9821 | 101 |
| 200244-moderate-udp | 59/59 | 103.5 / 120.3 ms | 146 ms | 0.9603 | 101 |
| 200452-harsh-udp | 59/59 | 202.8 / 240.0 ms | 787 ms | 0.8823 | 101 |
| 200701-lan-gns | 59/59 | 9.1 / 12.2 ms | 31 ms | 0.9860 | 101 |
| 200909-moderate-gns | 59/59 | 96.1 / 115.6 ms | 397 ms | 0.9425 | 101 |
| 201117-harsh-gns | 59/59 | 188.3 / 223.2 ms | 2279 ms | 0.8495 | 101 |
| 201326-lan-udp-sec | 59/59 | 10.5 / 12.6 ms | 50 ms | 0.9866 | 101 |
| 201534-moderate-udp-sec | 59/59 | 104.3 / 121.2 ms | 945 ms | 0.9454 | 101 |
| 201743-harsh-udp-sec | 59/59 | 203.0 / 237.0 ms | 1673 ms | 0.8395 | 101 |
| 201951-lan-gns-sec | 59/59 | 9.1 / 12.2 ms | 66 ms | 0.9855 | 101 |
| 202159-moderate-gns-sec | 59/59 | 95.1 / 114.9 ms | 685 ms | 0.9425 | 101 |
| 202408-harsh-gns-sec | 59/59 | 190.5 / 230.6 ms | 2560 ms | 0.8433 | 101 |
| *repeats* | | | | | |
| 202616-harsh-gns-sec | 59/59 | 187.9 / 227.4 ms | 2820 ms | 0.8370 | 111 |
| 202825-harsh-udp-sec | 59/59 | 205.0 / 240.9 ms | 2906 ms | 0.8356 | 111 |
| 203033-harsh-gns-sec | 59/59 | 189.9 / 226.6 ms | 2082 ms | 0.8437 | 121 |
| 203242-harsh-udp-sec | 59/59 | 204.9 / 239.0 ms | 2387 ms | 0.8363 | 121 |
| 203450-harsh-gns-sec | 59/59 | 189.1 / 231.7 ms | 1380 ms | 0.8482 | 131 |
| 203659-harsh-udp-sec | 59/59 | 203.5 / 237.0 ms | 1847 ms | 0.8499 | 131 |
| 203907-harsh-gns-sec | 59/59 | 189.9 / 235.2 ms | 2276 ms | 0.8477 | 141 |
| 204115-harsh-udp-sec | 59/59 | 203.4 / 239.0 ms | 1921 ms | 0.8422 | 141 |

- **20/20 passed.** This is **not** evidence of a fix: the session and transport behaviour is the same as at `4cf1935`; only instrumentation and harness changed.
- **The failure rate**, pooled over every 120 s run with the default policy since `4cf1935`:
  - review #11 battery and repeats, the arm A of EXP-025, and this battery;
  - harsh-gns-secure: **4 of 14** over 3000 ms (about 29%);
  - harsh-udp-secure: **2 of 15** (about 13%).

  KI-35 stays open.
- The realized conditions match the profile: client2 delivery 0.84–0.85 against 15% loss and 5% duplication, and RTT p50 about 190 ms (GNS) and 203 ms (UDP), for 80 ms ± 40 ms each way.

## Predeclared experiment FB-13-01: transport-specific reconnect retry (2026-10-24, EXP-026)

Written and committed **before** its first run.

- **Question.** In EXP-025, 250 ms helped UDP strongly and GNS not at all. Is a **UDP-only** shorter session reconnect retry better on UDP when tested on fresh seeds? On GNS the retry stays 500 ms, with no GNS arm.
- **One variable:** the client's `reconnect_retry_us`.
  - A = 500 ms (the default);
  - B = 250 ms, set with `soak.ps1 -ReconnectRetryMs 250`.

  The build and the gate (v6) are the same for both arms.
- **Runs:**
  - harsh-udp-secure, 120 s;
  - UDP seed bases 300, 310, …, 370 (8 seeds, none used in EXP-025), each once with A and once with B;
  - A and B interleaved per seed;
  - 16 runs in total.
- **Decision rule, fixed now,** applied by `tools/soak/compare_arms.py`. B becomes the UDP default only if:
  1. B has no more `client2_recovery_time` failures than A;
  2. B's median recovery is at least 10% lower;
  3. B introduces no other gate failure that A does not have.

  Aborted runs (no summary) are reported apart and excluded from every count. Otherwise A stays the default on UDP too.
- **GNS, alongside:** 8 more default harsh-gns-secure runs (no arm). Their phase distribution goes through `timeline.py --summary`, which measures where the GNS tail lives (GNS establishment, DTLS, session) before any GNS-side change is proposed.
- **The 3000 ms gate does not change either way.**

**EXP-026 result (`compare_arms.py`): the rule keeps A. B is not adopted.**

| Arm | n | Recovery median / max | Over 3000 ms |
|---|---|---|---|
| A 500 ms | 8 | 1632 / 2579 ms | 0 |
| B 250 ms | 8 | 1918 / 2383 ms | 0 |

- The EXP-025 UDP gain (median 2330 → 1163 ms on 5 runs) **did not replicate** on 8 fresh seeds. It was most probably small-sample chance. 500 ms stays the default on both transports.
- No arm was aborted, and realized delivery and RTT were comparable.

## Gate v6 at the final head of the stack (2026-10-25, EXP-026)

On `8d553a3`, after reviews #13 and #14. The gates are unchanged.

| Run (120 s, `8d553a3`) | Gates | RTT p50 / p99 (client1) | Recovery | client2 delivery | Host seed |
|---|---|---|---|---|---|
| 003832-lan-udp | 59/59 | 10.1 / 12.2 ms | 19 ms | 0.9804 | 101 |
| 004041-moderate-udp | 59/59 | 103.5 / 119.9 ms | 172 ms | 0.959 | 101 |
| 004249-harsh-udp | 59/59 | 203.8 / 237.8 ms | 797 ms | 0.8834 | 101 |
| 004457-lan-gns | 59/59 | 9.1 / 12.1 ms | 33 ms | 0.989 | 101 |
| 004706-moderate-gns | 59/59 | 95.7 / 116.6 ms | 391 ms | 0.9466 | 101 |
| 004914-harsh-gns | 59/59 | 191.9 / 231.9 ms | 747 ms | 0.8535 | 101 |
| 005123-lan-udp-sec | 59/59 | 10.5 / 12.2 ms | 49 ms | 0.9868 | 101 |
| 005331-moderate-udp-sec | 59/59 | 105.3 / 121.7 ms | 1423 ms | 0.9438 | 101 |
| 005539-harsh-udp-sec | 59/59 | 204.4 / 235.5 ms | 1157 ms | 0.8365 | 101 |
| 005748-lan-gns-sec | 59/59 | 9.1 / 12.2 ms | 66 ms | 0.9885 | 101 |
| 005956-moderate-gns-sec | 59/59 | 96.5 / 116.1 ms | 911 ms | 0.9426 | 101 |
| 010205-harsh-gns-sec | 59/59 | 189.2 / 231.0 ms | 1625 ms | 0.8523 | 101 |
| *repeats* | | | | | |
| 010413-harsh-gns-sec | 58/59 **FAILED: client2_recovery_time** | 188.3 / 235.9 ms | 4448 ms | 0.8436 | 511 |
| 010622-harsh-udp-sec | 58/59 **FAILED: client2_recovery_time** | 203.1 / 238.4 ms | 3158 ms | 0.8421 | 511 |
| 010830-harsh-gns-sec | 59/59 | 187.8 / 229.0 ms | 2324 ms | 0.8371 | 521 |
| 011039-harsh-udp-sec | 59/59 | 203.5 / 235.9 ms | 871 ms | 0.8475 | 521 |
| 011247-harsh-gns-sec | 58/59 **FAILED: client2_recovery_time** | 187.6 / 232.8 ms | 3837 ms | 0.8383 | 531 |
| 011456-harsh-udp-sec | 59/59 | 202.7 / 237.8 ms | 1200 ms | 0.8554 | 531 |
| 011704-harsh-gns-sec | 58/59 **FAILED: client2_recovery_time** | 187.3 / 226.8 ms | 3124 ms | 0.8506 | 541 |
| 011912-harsh-udp-sec | 59/59 | 202.6 / 238.0 ms | 2154 ms | 0.8387 | 541 |

- **12 scenarios: 12/12. Seeded repeats: harsh-gns-secure 3 of 4 failed, harsh-udp-secure 1 of 4 failed.** All failures are `client2_recovery_time` only.
- **Pooled at this head, default policy only** (EXP-026 arm A, the 8 GNS distribution runs, the battery and its repeats):
  - harsh-gns-secure: **5 of 13** over 3000 ms;
  - harsh-udp-secure: **1 of 13**.
- **GNS phase distribution** (`timeline.py --summary`, 13 reconnects):

  | Phase | p50 | p90 | max |
  |---|---|---|---|
  | notice | 195 ms | 453 ms | 455 ms |
  | establish | 886 ms | 1580 ms | 1749 ms |
  | DTLS authentication | 819 ms | 1529 ms | 1778 ms |
  | session | 690 ms | 1670 ms | 1681 ms |
  | total | 2588 ms | 3837 ms | 4448 ms |

  No single phase dominates: each of the three adds a second or so under loss.
- The hang watcher (`scripts/hang-watch.ps1`) recorded **no** stuck process during the whole run.

## Predeclared experiment FB-15-01: no DTLS cookie on GNS (2026-10-25, EXP-027)

Written and committed **before** its first run.

- **Question.** On GNS our DTLS cookie exchange costs a round trip, and the guarantee it gives is already provided by GNS itself:
  - an address-bound stateless challenge before a connection exists (F-GNS-001);
  - address-bound packet dispatch afterwards (F-GNS-002).

  Does skipping it reduce harsh-gns-secure recovery?
- **One variable:** the host's `--dtls-skip-cookie-if-routable`.
  - A = off (cookie, the default);
  - B = on (`soak.ps1 -SkipCookieIfRoutable`).

  Same build, gate v6, 120 s. UDP keeps its cookie in both arms by construction; no UDP arm is run.
- **Runs:**
  - harsh-gns-secure, **10 per arm**, A and B interleaved;
  - GNS fake loss is not seedable: the seed bases (600, 610, …, 690) are recorded but change nothing on GNS;
  - 20 runs in total.
- **Measures:** client2 recovery (median, p90, maximum, count over 3000 ms), the phase distribution (`timeline.py --summary`, where the cookie appears inside `dtls_auth`), and every other gate.
- **Decision rule, fixed now** (`compare_arms.py --arm skip_cookie_if_routable`). B becomes the GNS default only if **all** of these hold:
  1. B has no more recovery failures than A;
  2. B's median recovery is at least 10% lower;
  3. B introduces no other gate failure that A does not have;
  4. the security tests pass with the skip on:
     - `secure_cookie_skip_never_weakens_identity_or_udp`: UDP keeps its cookie, and an unpinned key or a wrong invitation is refused;
     - `secure_cookie_skip_keeps_pending_state_bounded`.

  Aborted runs are reported apart. Otherwise the cookie stays everywhere. **The 3000 ms gate does not change either way.**

**EXP-027 result (`compare_arms.py --arm skip_cookie_if_routable`, on `bd052d6`): the rule keeps A. B is not adopted.**

| Arm | n | Recovery median / max | Over 3000 ms |
|---|---|---|---|
| A cookie | 10 | 1992 / 4407 ms | 3 |
| B no cookie on GNS | 10 | 2274 / 3057 ms | 1 |

- Conditions 1, 3 and 4 held. Condition 2 failed: B's median was **14% higher**, not 10% lower.
- **The skip did what it should, and only that.**
  - Every B host logged `hello_verify_sent 0` and `cookie_skipped 3`; every A host sent HelloVerifyRequests.
  - The DTLS authentication phase fell by about one RTT (`timeline.py --summary`, 10 reconnects per arm):

    | Phase | A p50 / p90 | B p50 / p90 |
    |---|---|---|
    | notice | 198 / 517 ms | 195 / 437 ms |
    | establish | 842 / 1628 ms | 873 / 1369 ms |
    | DTLS authentication | 613 / 822 ms | 413 / 631 ms |
    | session | 213 / 702 ms | 221 / 711 ms |
    | total | 1859 / 3666 ms | 2194 / 2529 ms |

  - The total did not follow. GNS establishment and the session phase vary by far more than the 200 ms saved, so 10 runs per arm cannot show a 200 ms effect.
- B's lower failure count (1 against 3) and lower p90 are **not** a basis for adoption. The rule requires all four conditions, and it is not reinterpreted after the results.
- No run was aborted. Realized delivery (0.833–0.860) and RTT p50 (188–193 ms) were the same in both arms.
- The flag stays, off by default, as an experiment switch. The cookie stays on everywhere.
- The first EXP-027 attempt (`023741`) was cut at 1.5 s when the battery runner was killed by a session restart. It has no summary, is kept, and is not counted.

## Gate v6 at the head of review #15 (2026-10-25, EXP-027)

On `bd052d6`, with the default policy (cookie on, 500 ms retry). The gates are unchanged. Every process of every run exited by itself (`exit_status` all `exited`).

| Run (120 s, `bd052d6`) | Gates | RTT p50 / p99 (client1) | Recovery | client2 delivery | Host seed |
|---|---|---|---|---|---|
| 035016-lan-udp | 59/59 | 10.1 / 12.1 ms | 20 ms | 0.9806 | 101 |
| 035224-moderate-udp | 59/59 | 103.7 / 121.6 ms | 160 ms | 0.9591 | 101 |
| 035433-harsh-udp | 59/59 | 204.9 / 239.1 ms | 279 ms | 0.8828 | 101 |
| 035641-lan-gns | 59/59 | 9.1 / 12.1 ms | 35 ms | 0.9838 | 101 |
| 035849-moderate-gns | 59/59 | 95.9 / 117.6 ms | 383 ms | 0.9409 | 101 |
| 040058-harsh-gns | 59/59 | 189.5 / 235.9 ms | 1260 ms | 0.846 | 101 |
| 040306-lan-udp-sec | 59/59 | 10.1 / 12.1 ms | 547 ms | 0.9861 | 101 |
| 040514-moderate-udp-sec | 59/59 | 103.3 / 121.7 ms | 480 ms | 0.9452 | 101 |
| 040722-harsh-udp-sec | 59/59 | 204.1 / 236.9 ms | 1443 ms | 0.8365 | 101 |
| 040931-lan-gns-sec | 59/59 | 9.1 / 12.1 ms | 63 ms | 0.9857 | 101 |
| 041139-moderate-gns-sec | 59/59 | 95.8 / 115.9 ms | 1904 ms | 0.9451 | 101 |
| 041348-harsh-gns-sec | 59/59 | 190.5 / 234.3 ms | 2532 ms | 0.8398 | 101 |
| 041556-harsh-gns-sec | 59/59 | 190.5 / 225.4 ms | 2080 ms | 0.8505 | 711 |
| 041804-harsh-udp-sec | 59/59 | 203.4 / 238.1 ms | 926 ms | 0.8354 | 711 |
| 042013-harsh-gns-sec | 59/59 | 191.5 / 225.4 ms | 1369 ms | 0.844 | 721 |
| 042221-harsh-udp-sec | 59/59 | 203.7 / 237.8 ms | 2896 ms | 0.8552 | 721 |
| 042429-harsh-gns-sec | 59/59 | 187.5 / 228.8 ms | 1301 ms | 0.8451 | 731 |
| 042638-harsh-udp-sec | 59/59 | 202.9 / 237.0 ms | 1396 ms | 0.8454 | 731 |
| 042846-harsh-gns-sec | 59/59 | 189.0 / 223.4 ms | 2898 ms | 0.8389 | 741 |
| 043054-harsh-udp-sec | 59/59 | 203.4 / 236.5 ms | 2535 ms | 0.853 | 741 |

- **12 scenarios: 12/12. Seeded repeats: 8/8.** No gate failed. Two repeats came close to the limit: harsh-gns-secure 2898 ms and harsh-udp-secure 2896 ms.
- **Pooled at this head, default policy only** (EXP-027 arm A, the battery and its repeats):
  - harsh-gns-secure: **3 of 15** over 3000 ms (all three in EXP-027 arm A);
  - harsh-udp-secure: **0 of 5**.

  At `8d553a3` it was 5 of 13 and 1 of 13. These samples are small, and no change since then targets the recovery path for the default policy, so this is **not** read as an improvement.
- **GNS phases** of the 5 battery harsh-gns-secure reconnects (`timeline.py --summary`): p50 notice 185, establish 436, DTLS 560, session 674, total 2080 ms (p90 total 2898 ms).
- Test suites, both passes on both builds: all passed, with the transport check OK. E2E, asymmetric 4/4 and secure E2E 4/4 also passed. The hang watcher recorded no stuck process.

## Predeclared experiment KI-35: what sets GNS establishment time (2026-10-25, EXP-036)

Written and committed **before** its first run. Measurement only: no product, default or gate change.

- **Context.** In the 39 harsh-gns-secure reconnects recorded so far, `phase_establish` (from `reconnecting` to the
  first DTLS ClientHello, which waits for the inner GNS `Connected`) has p50 880 ms and p90 1580 ms. It is the largest
  phase. F-GNS-003, source-verified, describes how GNS connects: a 4-packet exchange that restarts every 500 ms; the
  500 ms is a compile-time constant, not a GNS setting.
- **Model** (`tools/ki35/model.py`, fixed seed, 200 000 samples, harsh mapping of `gns_transport.cpp`, duplicates not
  modelled). Raw establishment: **p50 409 ms, p90 1850 ms, p99 3360 ms**. Per-attempt success is 0.85^4 = 0.52.
- **Measurement** (`tools/ki35/probe`, GNS build). One process holds a listening GNS transport and a fresh client
  transport per trial. The harsh profile is applied through our own `set_impairment`; it is process-global, so both
  directions are impaired, as in the soak. Each trial times `connect()` to the client's `Connected` event, with both
  transports polled every 1 ms at 1 ms timer resolution. 300 harsh trials and 50 unimpaired trials.
- **Decision rules, fixed now:**
  - **H1 (the GNS model explains raw establishment):** supported if the measured raw p50 is in [259, 559] ms **and**
    the raw p90 in [1388, 2313] ms (each ±max(25%, 150 ms) of the model). If supported, the establishment tail is set
    by a GNS constant we cannot configure, and any remedy must avoid or overlap new connections, each in its own
    predeclared experiment.
  - **H2 (our reconnect path adds latency beyond raw GNS):** supported if 880 ms (the soak p50 above) exceeds the
    measured raw p50 by more than 200 ms. If supported, the next predeclared step instruments the client between
    `reconnecting`, the GNS `connect()` call and the inner `Connected`. Instrumentation only.
  - Neither result changes a gate, a default or the protocol.

**EXP-036 result (`run_probe.py` on `b36d226`): H1 not supported, H2 not supported.**

| | p50 | p90 | p99 |
|---|---|---|---|
| Model (F-GNS-003) | 409 ms | 1850 ms | 3360 ms |
| Raw GNS, measured (300 harsh trials, 0 failures) | **844 ms** | **1598 ms** | 3210 ms |
| Soak `phase_establish` (39 reconnects) | 880 ms | 1580 ms | - |

- **By the rules:**
  - H1 fails on its p50 band; its p90 is inside the band.
  - H2 fails: the soak's p50 exceeds the raw p50 by about 37 ms. That compares medians of different samples (the probe and earlier soak reconnects): it is not a paired estimate and does not bound what our reconnect path adds (corrected after review #25).
- **Post-hoc, not a rule:** the successes fall in 500 ms retry cycles as F-GNS-003 describes. In the first cycle 149/300 = 0.50 succeed; the model predicted 0.52. With a first-attempt rate this close to 0.5, the median sits on the boundary between cycle 1 (about 400 ms) and cycle 2 (about 850 ms). So the p50 band was a fragile criterion. The rule's result stands; future predeclarations judge per-attempt rates, not a median near such a boundary.
- **What it means for KI-35:** the establishment phase is GNS's own fixed 500 ms retry, a compile-time constant. How much of it our polling and connection lifecycle add is not measured by this unpaired comparison. Remedies would avoid or overlap new GNS connections, or patch GNS (a dependency change, owner approval). Each would need its own predeclared experiment. The DTLS (p50 613 ms) and session (p50 674 ms) phases are ours and are not yet decomposed.

## Predeclared experiment KI-35: DTLS and session phases per connection (2026-10-25, EXP-037)

Written and committed **before** its first run, after review #25 (FB-25-01..03). Measurement only: no product, default
or gate change.

- **Question.**
  - Do the DTLS and session phases, which are ours, behave as their packet-level mechanisms predict?
  - Which phase makes each slow connection slow? Phases are correlated per connection, not by adding medians of
    unrelated samples.
- **Probe** (`tools/ki35/secure_probe.cpp`, GNS build).
  - A secure Host and Client in one process, in real time (1 ms loop at 1 ms timer resolution), harsh profile.
  - Each trial is a fresh secure join, then a leave. **A fresh join is not a stall reconnect:** it has no notice
    phase and no token.
  - Per trial: `establish` (`connect()` to the first DTLS ClientHello), `dtls` (to `authenticated`), `session` (to the
    client's `Connected`) and `total`.
  - Counts per trial: client DTLS retransmission timers, ClientHellos, the session's ConnectRequests (from the
    client's `connect_accept_received` log line) and, on GNS, the time of the inner `Connected`.
- **Batches.**
  - UDP: seeds 3701, 3702, 3703 (host and client impairment seeded), 100 trials each.
  - GNS: 3 repetitions of 100 trials. GNS fake loss cannot be seeded, so these are repetitions, not seeds.
- **Model** (`tools/ki35/model.py` `proportions()`). Per-attempt success, as [without, with] duplicates; a duplicate
  is assumed to be lost independently:
  - `dtls_no_timer` **[0.377, 0.394]**: all 6 handshake flights arrive (cookie on; each flight one coalesced datagram).
  - `session_first` **[0.614, 0.628]**: one ConnectRequest suffices; needs the client's binding, the ConnectRequest
    and the ConnectAccept. Alternative `session_first_2leg` [0.723, 0.733].
  - GNS `gns_first_cycle` **[0.522, 0.538]**: the inner connection completes within its first 500 ms cycle (inner
    `Connected` before 800 ms, the EXP-036 classification).
- **Decision rules, fixed now.**
  - For each proportion, pooled per backend, compute the 95% Wilson interval. The mechanism is **supported** if that
    interval intersects the model's [without, with] interval, **not supported** otherwise.
  - For the session, report which of the 3-packet and 2-packet intervals it intersects: both, one or none.
  - Reproducibility: per-batch intervals are reported. The batches are **consistent** if every pair of per-batch
    95% intervals overlaps.
  - User-facing latency (FB-25-02): per backend, n, failures, and p50/p90/p95/p99 of each phase and of the total.
    Among the slowest 10% of totals, count which phase was the largest.
  - No result here changes a gate, a default or the protocol, nor closes KI-35.

**EXP-037 result (`ki35-secure-2026-10-25-222913-b4d584f-dirty`, 600/600 joins connected).**

| Mechanism (predeclared) | Model | UDP, 95% Wilson | GNS, 95% Wilson | Rule |
|---|---|---|---|---|
| DTLS without a client retransmission | 0.377-0.394 | 0.363 [0.311, 0.419] | 0.413 [0.359, 0.470] | **supported** (both) |
| Session with exactly one ConnectRequest | 0.614-0.628 (2-pkt 0.723-0.733) | 0.353 [0.301, 0.409] | 0.547 [0.490, 0.602]* | **not supported** (both; neither model) |
| GNS inner within its first cycle | 0.522-0.538 | - | 0.590 [0.534, 0.644] | **supported** |

\* From the corrected re-analysis (`ki35-secure-...-reanalysis-6b116e2`). The original verdict counted one unsent pre-authentication request (`sent: false`), giving 163/300 = 0.543 [0.487, 0.599]. No rule outcome changes, and the run and its verdict are unchanged.

- The batches are consistent for every proportion: UDP seeds 3701–3703 and the GNS repetitions.
- **Latency per connection** (ms, p50 / p90 / p95 / p99):

  | Phase | UDP | GNS |
  |---|---|---|
  | establish | 0.6 / 0.8 / 0.9 / 3.7 | 408 / 1443 / 2124 / 3644 |
  | DTLS | 857 / 1926 / 2593 / 4231 | 835 / 1812 / 2266 / 3657 |
  | session | 445 / 718 / 949 / 1431 | 235 / 722 / 956 / 1271 |
  | total | 1312 / 2542 / 3215 / 4672 | 1955 / 3397 / 4186 / 6066 |

- **The slowest 10% of connections:** DTLS is the largest phase in 29 of 30 on UDP, and in 16 of 30 on GNS (establishment in the other 14).
- **Post-hoc, not a rule:**
  - The client sends a ConnectRequest at the instant of authentication, then every **250 ms**: the initial-connect interval, `connect_retry_us`, which is close to one harsh round trip.
  - A first request that succeeds is often followed by a redundant second one before its ConnectAccept arrives. So "exactly one request" undercounts first-try successes: the metric was mis-specified.
  - These are fresh joins. KI-35 concerns reconnects, which retry every 500 ms.
- **Probe defects found after the run** (Codex, PR #25). All are fixed, and none affects a rule:
  - The ClientHello count also matched Certificate and ClientKeyExchange records. No rule or figure above uses it.
  - The probe lacked the Windows 11 power-throttling opt-out for its 1 ms timer (KI-39). Checked on the recorded client logs: the 250 ms ConnectRequests are 250–253 ms apart, so polling held at about 1–2 ms. The fixed probe records `loop_ms`. The EXP-036 probe got the same fix.
  - There was no GNS shutdown guard on startup-error exits. Every batch exited 0, so none was taken.
- **For KI-35:** DTLS dominates the slow tail and behaves as its loss model predicts. Its timing is the next candidate: the retransmission back-off (250 ms initial, doubling, capped at 1 s), and the cookie round trip (EXP-027). The session phase should be re-measured on the reconnect path, counting the request whose accept completed the session.

## Predeclared experiment KI-35: phases of each stall reconnect (2026-10-26, EXP-041)

This was written and committed **before** the first run, after the EXP-037 follow-up review (FB-25-05..08). It is measurement only: no protocol, default or gate change. The margin δ below was chosen after EXP-037's results were known; this is disclosed.

**Question.** On the path KI-35 is about (a stalled client's secure reconnect), which phase makes slow reconnects slow? Do the DTLS, session and GNS mechanisms match their packet-level models when judged by equivalence, not by overlap?

**Probe** (`tools/ki35/reconnect_probe.cpp`, GNS build):
- A secure Host and Client run in one process, in real time (a 1 ms loop at 1 ms timer resolution, with the power-throttling opt-out; `loop_ms` is recorded), under the harsh profile. Defaults are unchanged: client and host timeouts 3 s, reconnect retry 500 ms, reconnect window 15 s.
- **Cycle:**
  1. 1 s connected.
  2. A **5 s stall**, as in the soak: the client is not updated, while the host and the GNS thread run.
  3. Then both are updated until the client is `Connected` again after having been `Reconnecting`.
  4. 1 s connected.
- **Bad cycles:**
  - A cycle not reconnected within 20 s of the stall end is a **timeout**. A client left `Failed` is replaced by a fresh join before the next cycle.
  - A cycle that never left `Connected` is **no_reconnect**. It is counted and excluded from the phases.
- **Per reconnect, from the stall end t0:**
  - The phases:
    - `notice`: t0 to `Reconnecting`;
    - `establish`: to the first ClientHello sent after that;
    - `dtls`: to `authenticated`;
    - `session`: to `Connected`.
  - `total` (t0 to `Connected`: the soak's recovery definition) and `first_snapshot` (t0 to the first snapshot applied after `Connected`).
  - Descriptive: `cookie_ms` (first ClientHello to the ClientHello with the cookie), client DTLS timers and ClientHellos before authentication, and on GNS the inner `Connected`.
- **Session attribution (FB-25-08), by timestamps only:**
  - Take the client's `connect_accept_received` log time A, and the ConnectRequests actually sent (`sent: true`) after authentication.
  - A request is a candidate cause if A − its send time is in **[140, 260] ms**: the harsh round trip of 160–240 ms ± 20 ms of processing. Exactly one candidate means attributed. Zero or several means unattributed.
  - `session_first` means the attributed request is the first one sent after authentication.

**Batches:** UDP seeds 4101, 4102 and 4103, and 3 GNS repetitions (GNS loss cannot be seeded); 100 cycles each.

**Models** (`tools/ki35/model.py` `proportions()`, unchanged; [without, with] duplicates):
- `dtls_no_timer` [0.3771, 0.3944];
- `session_first` [0.6141, 0.628], with the 2-packet alternative [0.7225, 0.7334];
- GNS `gns_first_cycle` [0.522, 0.5378]: inner `Connected` within 800 ms of `Reconnecting`.

**Decision rules, fixed now:**
- **Homogeneity first.** For each proportion and backend, run a chi-square test across the 3 batches (df = 2, exact p = e^(−X²/2)). If p < 0.01 the batches are **heterogeneous**: no pooled verdict, only per batch.
- **Equivalence (TOST, α = 0.05, δ = 0.08)** on the pooled count:
  - **equivalent** if the 90% Wilson interval lies inside [model_without − δ, model_with + δ];
  - **different** if the 95% Wilson interval misses [model_without, model_with];
  - otherwise **inconclusive**.
  - For the session, judge both the 3-packet and the 2-packet model.
  - If more than 10% of reconnects are unattributed, `session_first` is **not assessable**.
- **Independence:** the trials of a batch run in one process, one after another. The lag-1 autocorrelation of each 0/1 outcome is reported per batch; it is not a rule.
- **Tail (FB-25-02, FB-25-07):**
  - Per backend and per batch: n, timeouts, no_reconnect, and p50 / p90 / p95 / p99 / max of every phase, `total` and `first_snapshot`.
  - P(`total` > 3000 ms, the soak's recovery limit) with a 95% Wilson interval.
  - Among the slowest 10% of `total`s, count which phase is largest. A phase **dominates the tail** if it is largest in at least 50% of them.
- No result changes a gate, a default or the protocol, nor closes KI-35. DTLS timers and the cookie get their own experiments afterwards (FB-25-07).

**EXP-041 result (`ki35-reconnect-2026-10-26-114558-c5cc5f9`, 600/600 reconnected).** Analysed with `ad7d08b`, whose only change is the 50/50 tail rule, in `…-reanalysis-ad7d08b`. The run is complete and every proportion is homogeneous across its batches (p ≥ 0.019).

| Mechanism | Model | UDP | GNS | Verdict (TOST, δ = 0.08) |
|---|---|---|---|---|
| DTLS without a retransmission | 0.377–0.394 | 0.347, 90% [0.303, 0.393] | 0.383, 90% [0.338, 0.430] | **equivalent** (both) |
| GNS inner within its first cycle | 0.522–0.538 | - | 0.527, 90% [0.479, 0.574] | **equivalent** |
| Session: the first request completed it (attributed) | 0.614–0.628 (2-pkt 0.723–0.733) | 0.360, 95% [0.308, 0.416] | 0.557, 95% [0.500, 0.612] | **different** (both models) |

- **Attribution:** 0 of 600 reconnects unattributed. No timeouts, failures or missed reconnects.
- **Reconnect latency** (ms, p50 / p95 / p99):

  | Phase | UDP | GNS |
  |---|---|---|
  | notice | 106 / 402 / 630 | 197 / 481 / 689 |
  | establish | 3 / 3 / 3 | 427 / 1852 / 3264 |
  | DTLS | 852 / 2557 / 3307 | 820 / 2525 / 3496 |
  | session | 695 / 1218 / 2188 | 213 / 1210 / 1718 |
  | **total** | **1677 / 3639 / 4353** | **2449 / 4490 / 5517** |

- **Over the soak's 3 s limit:** UDP 8.3% [5.7, 12.0], GNS 27.7% [22.9, 33.0].
- **The slowest 10% of reconnects:** DTLS **dominates the tail** on both backends: it is the largest phase in 26 of 30 on UDP, and in 17 of 30 on GNS (establishment in 10).
- **Post-hoc, not a rule:** why the first session request so often fails.
  - When its handshake finishes, the client sends its binding. In the same update it verifies the host's binding, which arrived in the same flight, and sends the first ConnectRequest **right after its binding**.
  - The host drops application records that arrive before the binding (`app_dropped_before_auth`, fail-closed). Jitter reorders the two back-to-back datagrams: about half the time under our UDP impairment (independent U[0, 40] ms each), less on GNS (reorder 20%).
  - That predicts about 0.31 on UDP and 0.5–0.55 on GNS, against 0.36 and 0.557 observed. It also explains EXP-037's fresh joins (0.353 and 0.547). Each lost first request costs a 500 ms retry on reconnect.
  - **This is a hypothesis.** Next: a predeclared experiment counting the host's `app_dropped_before_auth` per reconnect. Any fix (for example, the first request waits for an acknowledged binding, or the host holds at most one early record until the binding verifies, still fail-closed) is a later decision for the owner.

## Predeclared experiment KI-35: does the first session request overtake the binding? (2026-10-26, EXP-042)

This was written and committed **before** the first run. It tests EXP-041's post-hoc explanation and is measurement only. The single code change is one diagnostic trace event (`app_dropped_before_auth`) at the point where `DtlsSession` already drops and counts the record; behaviour is unchanged.

**Hypothesis H.**
- When its handshake finishes, the client sends its binding. In the same update it authenticates on the host's binding and sends the first ConnectRequest right after its own binding, with the same send time.
- The host drops application records that arrive before the binding, and jitter reorders the two.
- So the first request is often dropped, and a dropped one almost never completes the session.

**Probe:** `tools/ki35/reconnect_probe.cpp`, unchanged from EXP-041 except that it also records the host's DTLS trace: the times of `app_dropped_before_auth` and of the host's `authenticated`. Same cycle, profile and defaults (5 s stall, harsh).

**Measured per reconnect:**
- **D1:** the host dropped at least one copy of the first request. That is a host drop 70–130 ms after the client logged the first sent ConnectRequest after authentication (one way is 80–120 ms; the next request comes 500 ms later).
- **first:** the first request completed the session, attributed by timestamps as in EXP-041.
- Reconnects without a logged first request are counted and excluded.

**Model** (`tools/ki35/model.py --reorder`, UDP impairment semantics: loss per send, a duplicate with its own delay, 80 + U[0, 40] ms; 1,000,000 samples, seed 42). Each model interval is the value ± 0.002.

| Quantity | H | If delivery kept send order |
|---|---|---|
| P(D1) | **0.4888** | 0.1274 |
| P(first) | **0.3183** | – |
| P(first \| D1) | **0.0208** | – |
| P(first \| not D1) | **0.6028** | – |

**Batches:** UDP seeds 4201, 4202 and 4203, and 3 GNS repetitions, 100 cycles each.

**Decision rules, fixed now:**
- **Completeness and homogeneity** as in EXP-041: verdicts only for a complete run, and per batch if the chi-square p < 0.01.
- **UDP is the test** (its impairment is ours and modelled exactly). TOST with δ = 0.08 is primary; verdicts at δ = 0.05 and 0.10 are reported as sensitivity.
  - **R1:** P(D1) is equivalent to H, and different from the in-order value (its 95% interval misses 0.1274).
  - **R2:** P(first) is equivalent to H.
  - **R3:** P(first | D1) and P(first | not D1) are each equivalent to H.
  - **H is supported** if R1, R2 and R3 all hold. **H is refuted** if any is "different". Otherwise the result is **inconclusive**.
- **GNS, directional only** (GNS jitter keeps order and reorders 20% of packets by a fixed delay, which is not modelled exactly):
  - P_GNS(D1) < P_UDP(D1), with disjoint 95% intervals;
  - and the upper 95% bound of P_GNS(first | D1) is below the lower 95% bound of P_GNS(first | not D1).
- **Reported:** host `authenticated` minus the first request's arrival for D1 reconnects; the number of drops per reconnect.
- No result changes a gate, a default or the protocol. A fix (for example, the first request waits until the binding is verified, or the host holds one early record, still fail-closed) is an owner decision afterwards.

**Amendment, before any run** (a Codex review of the tooling, 2026-10-26). No data had been collected: only two 4-cycle smoke runs, which tested the pipeline and are not counted. The rules above are replaced where they differ.
- **Model corrected.** A duplicate is the same DTLS record, and OpenSSL's replay window drops a record already read (`dtls12_replayed_and_tampered_records_are_dropped`). So only each record's earliest copy matters, and a processed request gets exactly one accept. New values (`model.py --reorder`, same seed and sample count):

  | Quantity | H | If delivery kept send order |
  |---|---|---|
  | P(D1) | **0.4886** | 0.1273 |
  | P(first) | **0.3067** | – |
  | P(first \| D1) | **0** (a dropped request cannot complete) | – |
  | P(first \| not D1) | **0.5997** | – |

- **A session that never completed** after authentication counts as first = false; it is never removed. If the completing request is unknown (no accept, no candidate or ambiguous) for more than 10% of eligible reconnects, R2 and R3 are **not assessable**.
- **H is refuted only by an actual "different" result.** It is **supported** only if R1–R3 are all equivalent and the in-order contrast is "different". Missing data or an unestablished contrast is **inconclusive**.
- **Homogeneity applies everywhere.** A heterogeneous UDP proportion gets per-batch verdicts only. The GNS directional rules are not judged if any proportion they use (UDP D1, GNS D1, GNS P(first | D1), GNS P(first | not D1)) is heterogeneous.

**EXP-042 result (`ki35-reorder-2026-10-26-153230-1683fe2`).** The run was complete, and analysed with `7e148ce` (gates only; the rule results are identical) in `…-reanalysis-7e148ce`. 600 reconnects; 1 of 600 had an unknown completing request; every proportion was homogeneous (p ≥ 0.025).

| UDP (the test) | H | Observed | Verdict (δ = 0.08) |
|---|---|---|---|
| R1 P(D1) | 0.4886 | 0.450, 90% [0.403, 0.498] | **inconclusive** (equivalent at δ = 0.10, inconclusive at 0.05); in-order 0.127 **rejected** |
| R2 P(first) | 0.3067 | 0.333 | **equivalent** |
| R3 P(first \| D1) | 0 | **0 / 135** | **equivalent** |
| R3 P(first \| not D1) | 0.5997 | 0.606 | **equivalent** |

- **H is inconclusive by its predeclared rule.** Only R1's equivalence fails narrowly: the 90% lower bound is 0.403 against the required 0.407. Every other rule holds, and the in-order alternative is rejected.
- **The sharpest prediction held without exception:** no dropped first request ever completed the session, 0 of 135 on UDP and 0 of 66 on GNS.
- **GNS, directional:** both rules hold. P(D1) is 0.22, below UDP's 0.45, and a dropped first request never completes (0/66), while an undropped one does in 70% (164/233).
- **At the host,** the binding arrived a median 19 ms after the dropped request on UDP (p95 487 ms, when the binding was lost and resent), and 204 ms on GNS.
- **Not explained:** D1 runs about 0.04 below the model. The three UDP batches each had exactly 45 D1 reconnects; their per-cycle sequences differ, so this is a coincidence.
- **For KI-35:** a lost first request costs a 500 ms retry, in about 45% of harsh UDP reconnects and 22% on GNS. A fix that keeps fail-closed binding is the next step, and an owner decision: either the client waits until its binding is verified before the first request, or the host holds at most one early record unprocessed until the binding verifies.

## Predeclared experiment KI-35: A/B of client-side gating (2026-10-26, EXP-043)

This was written and committed **before** any run, after reviews #27/#28 (FB-28-01/02). The owner authorised the behaviour change behind a flag that is off by default. **No default changes here**: adopting the flag is a later owner decision.

**Arms:** `off` (current behaviour) and `on` (`SecureTransportConfig::confirm_binding` on host and client: the host acks the client's verified binding, and the client authenticates only after that ack; see `docs/protocol.md`).

**Probe:** `tools/ki35/reconnect_probe.cpp` as in EXP-042, plus an arm and a profile argument, recorded in each cycle and in `run.json`. Same 5 s stall cycle and defaults.
- **harsh:** 15% loss, 5% duplicates, 80 ms + U[0, 40] ms. UDP seeds 4301, 4302 and 4303 and 3 GNS repetitions, **per arm**, 100 cycles each. The arms alternate batch by batch (off, on, off, on …) so drift affects both.
- **latency:** 80 ms + U[0, 40] ms, no loss, no duplicates; one batch of 50 cycles per arm and backend. This prices the gate's extra round trip on a clean path.
- The UDP arms share seeds, but their random streams diverge as behaviour differs. They are analysed as independent samples.

**Per reconnect** (as in EXP-041/042):
- the phases and `total` (stall end to `Connected`); a reconnect that never connects counts as 20,000 ms (the cycle limit), a conservative censoring;
- sent ConnectRequests after authentication;
- D1 (the host dropped the first request);
- client `gate_timeout` events.

**Rules, fixed now** (per backend; harsh unless stated):
- **Completeness:** all 16 batches, with contiguous cycles, exit 0, the arm and profile recorded, and the host trace present. Otherwise no verdict.
- **Homogeneity:** each arm's 3 harsh batches are checked by chi-square (df = 2) on P(total > 3 s) and on D1. If p < 0.01, that outcome gets per-batch reporting only.
- **O1 (primary) P(total > 3000 ms):** the difference on − off with a 95% Newcombe hybrid score interval. **on better** if the upper bound is < 0, **on worse** if the lower bound is > 0, otherwise **no difference shown**.
- **O2 p95 and p99 of total:** the difference on − off with a seeded bootstrap (seed 43, 4000 resamples, percentile 95% interval). Better, worse or no difference as in O1.
- **O3 cost:** the difference in p50 of total on − off, bootstrap as in O2, for **both** profiles. On the latency profile this is the gate's price, and it is reported, not judged.
- **O4 security:** in arm `on`, a reconnect with D1 = 1 and no client gate timeout is a **defect** (the gate let a record through to a host that dropped it). Any defect blocks a recommendation.
- **Reported:** session-phase p50/p95; ConnectRequests per reconnect; timeouts and failures per arm; gate timeouts.
- **Recommendation for the owner, not automatic.** Recommend adoption only if O1 or O2-p95 shows **on better** on at least one backend, **on worse** appears on no backend for O1 or O2, and O4 has no defect. Otherwise do not recommend. KI-35 stays open either way until the soak gate says otherwise.

**EXP-043 result: negative (`ki35-ab-2026-10-26-184205-a68552d`, complete).** No improvement in total recovery was shown, so the default stays off (FB-29-04). The faster session phase moves time into DTLS; it is not a recovery gain. 1200 harsh and 200 latency-only reconnects. There were no timeouts, failures or gate timeouts, and every batch was homogeneous (p ≥ 0.013).

| | UDP off | UDP on | GNS off | GNS on |
|---|---|---|---|---|
| First request dropped (D1) | 150/300 | **0/300** | 76/300 | **0/300** |
| ConnectRequests per reconnect | 2.04 | 1.35 | 1.67 | 1.36 |
| Session phase p50 (ms) | 702 | **208** | 223 | 195 |
| Total p50 / p95 / p99 (ms) | 1817 / 3365 / 4867 | 1666 / 3557 / 4919 | 2445 / 4492 / 6059 | 2478 / 4775 / 6321 |
| P(total > 3 s) | 25/300 | 32/300 | 88/300 | 81/300 |

- **O4 security:** 0 defects and 0 unassessable reconnects. The gate removed every first-request drop, as designed.
- **O1, P(total > 3 s):** no difference shown. UDP +0.023, 95% [−0.024, +0.071]; GNS −0.023, [−0.095, +0.049].
- **O2, p95 and p99:** no difference shown on either backend. For example, the UDP p95 difference is +192 ms, [−615, +698].
- **O3, p50 cost (reported):**
  - harsh: UDP −151 ms [−248, +108], GNS +33 [−139, +165];
  - latency only: UDP −198 [−302, +180], GNS **+185 [+164, +202]**.
  - That GNS figure is the gate's round trip where the first request was rarely dropped.
- **By the predeclared rule: do not recommend.**
  - Gating fixes the session phase, but the slow tail is DTLS and, on GNS, establishment (EXP-041), which it does not touch.
  - It moves the session's wasted retry into a wait for the ack inside DTLS.
  - `confirm_binding` stays off by default. Next for KI-35 are the DTLS-timer and cookie experiments (FB-25-07 / FB-28-04).

## Predeclared experiment KI-35: DTLS retransmission back-off (2026-10-27, EXP-044)

This was written and committed **before** any tooling or run, after review #29 (FB-29-02). **No default changes here**: any change is a later owner decision.

**Question.** Does the growth of the DTLS handshake retransmission timer drive KI-35's tail? EXP-041 shows DTLS as the largest phase in most of the slowest reconnects on both backends. Today a flight is retransmitted after 250 ms, then 500 ms, then every 1 s (`DtlsConfig::retransmit_initial_us` / `retransmit_max_us`, EXP-022). Under 15% loss, a flight lost twice in a row costs 750 ms of waiting, and three times 1750 ms.

**Arms, one variable** (the cap of the back-off, on host and client):
- `off`: today's defaults (250 ms initial, doubling, 1 s cap), as in EXP-043's `off` arm.
- `flat`: the same 250 ms initial with the cap at 250 ms, so the timer never grows.

`flat` is a **bounding** arm, not a candidate default. RFC 6347 §4.2.4.1 recommends doubling the timer, to avoid congestion on real links. If even no growth at all does not move the tail, no milder setting will, and the timer is not KI-35's lever. If it does, a milder setting is predeclared next, and the result alone changes nothing.

**Probe:** `tools/ki35/reconnect_probe.cpp` as in EXP-043, with arm `flat` (`SecureTransportConfig::retransmit_max_us = 250'000` on both sides; plumbing only, defaults unchanged). Per cycle it also records:
- the host's DTLS timer fires;
- the DTLS datagrams sent by each side from the stall end until that side authenticates (`tx` traces). This is the wire cost; bytes are not traced, and a retransmission resends the same flight;
- the process CPU time so far.

**Batches,** arms alternating batch by batch (off, flat, off, flat …) so drift affects both:
- **harsh** (15% loss, 5% duplicates, 80 ms + U[0, 40] ms): UDP seeds 4401, 4402 and 4403 and 3 GNS repetitions, **per arm**, 100 cycles each;
- **latency** (80 ms + U[0, 40] ms only): one batch of 50 cycles per arm and backend. This prices spurious retransmissions on a clean path, where a round trip can exceed 250 ms.
- The UDP arms share seeds, but their random streams diverge as behaviour differs. They are analysed as independent samples.

**Per reconnect** (as in EXP-041/043): the phases; `total` (stall end to `Connected`; a reconnect that never connects counts as 20,000 ms, a conservative censoring); the DTLS phase (first ClientHello to client authentication); ClientHellos sent; client and host timer fires; handshake datagrams of both sides.

**Rules, fixed now** (per backend; harsh unless stated):
- **Completeness:** all 16 batches, with contiguous cycles, exit 0, the arm and profile recorded, and the host trace present. Otherwise no verdict.
- **Homogeneity:** each arm's 3 harsh batches are checked by chi-square (df = 2) on P(total > 3 s). If p < 0.01, that outcome gets per-batch reporting only.
- **O1 (primary) P(total > 3000 ms):** the difference flat − off with a 95% Newcombe hybrid score interval. **flat better** if the upper bound is < 0, **flat worse** if the lower bound is > 0, otherwise **no difference shown**.
- **O2 p95 and p99 of total:** the difference flat − off with a seeded bootstrap (seed 43, 4000 resamples, percentile 95% interval). Better, worse or no difference as in O1.
- **O3 p50 of total** and **of the DTLS phase,** flat − off, bootstrap as in O2, for both profiles: reported, not judged.
- **C1 wire cost:** mean handshake datagrams (both sides) per reconnect, flat / off. **The cost guard fails** if that ratio is above 1.5 on any backend under harsh.
- **C2 robustness:** timeouts, failures and reconnects that never started, per arm. **The guard fails** if flat has more of them than off on any backend.
- **Reported:** ClientHellos and timer fires per reconnect; CPU time per cycle (the probe's 1 ms loop dominates it, so it is a sanity check, not a measure of DTLS).
- **Reading for the owner, not automatic:**
  - **"growth matters"** only if O1 or O2-p95 shows **flat better** on at least one backend, **flat worse** appears on no backend for O1 or O2, and both guards hold. Then a milder setting (for example a 500 ms cap) is predeclared next.
  - Otherwise **"the timer cap is not KI-35's lever"**, and the cookie round trip (FB-28-04) is next.
  - KI-35 stays open either way until the soak gate says otherwise.

**EXP-044 result: "growth matters" (`ki35-timer-2026-10-27-015836-75eb568`, complete, run on `75eb568`, clean).** With no back-off growth, the slow tail shrinks on both backends, at no measurable wire cost. The median does not move. 1200 harsh and 200 latency-only reconnects; no timeouts, failures or reconnects that never started; every harsh arm homogeneous (p ≥ 0.218). Re-analysing the directory gives an identical verdict.

| harsh | UDP off | UDP flat | GNS off | GNS flat |
|---|---|---|---|---|
| P(total > 3 s) | 20/300 | 11/300 | 103/300 | **57/300** |
| Total p50 / p95 / p99 (ms) | 1878 / 3349 / 4548 | 1782 / **2905** / **3577** | 2547 / 4412 / 5683 | 2369 / **3800** / **4705** |
| DTLS phase p50 / p95 / p99 (ms) | 848 / 2295 / 3571 | 866 / 1772 / 1994 | 818 / 2484 / 3958 | 816 / 1504 / 1812 |
| Handshake datagrams per reconnect | 10.69 | 11.02 | 10.30 | 10.14 |
| Timer fires (both sides) / ClientHellos per reconnect | 1.93 / 2.96 | 2.35 / 3.13 | 2.00 / 3.08 | 1.93 / 2.95 |

- **O1, P(total > 3 s):** GNS **flat better**, −0.153, 95% [−0.222, −0.083]. UDP no difference shown, [−0.068, +0.006].
- **O2:** flat better on both backends.
  - p95: UDP −444 ms [−953, −9], GNS −612 [−1280, −227];
  - p99: UDP −971 [−1601, −417], GNS −979 [−3040, −336].
- **O3, p50 (reported):**
  - harsh total: UDP −96 ms [−222, +42], GNS −178 [−295, +40]; DTLS phase: UDP +18 [−27, +157], GNS −2 [−37, +33];
  - latency only total: UDP +33 [−43, +444], GNS −10 [−36, +16].
  - On the clean path, timer fires per reconnect were 0.06 (off) vs 0.10 (flat) on UDP and 0.06 vs 0.00 on GNS. With a loopback RTT of 160–240 ms, the 250 ms timer barely fired spuriously.
- **Guards:** C1 datagram ratio UDP 1.030, GNS 0.985 (limit 1.5); C2 0 events in each category in both arms. Both hold.
- **Reported:** CPU per cycle 54.5 vs 50.1 ms (UDP) and 43.7 vs 45.1 ms (GNS). The probe loop dominates it.
- **Reading, by the predeclared rule: "growth matters."** The DTLS tail is driven by back-off growth: flat cuts the DTLS p99 roughly in half on both backends.
  - **No default changes.** `flat` is a bounding arm. RFC 6347 §4.2.4.1 recommends doubling to avoid congestion on real links. This loopback impairment does not model congestion, nor paths whose RTT exceeds 250 ms, where a flat 250 ms timer would retransmit every flight.
  - **KI-35 is not fixed.** Even with flat, 19% of harsh GNS reconnects take over 3 s: GNS establishment and the session phase remain (EXP-041).
  - **Next, by the rule:** predeclare a milder setting (for example a 500 ms cap) before any run, including a path whose RTT exceeds the timer. Adopting any setting is an owner decision.

## Predeclared experiment KI-35: intermediate DTLS back-off caps (2026-10-27, EXP-047)

This section was written and committed **before** any tooling or run, after review #31 (FB-31-01..04). **No default changes here**: any change is a later owner decision, followed by the soak gate.

**Question.** EXP-044 showed that back-off growth drives the DTLS tail, but its `flat` arm was only a bound. Do milder caps keep the tail gain? And what do they cost in bytes, in resent datagrams, and on a path whose RTT exceeds the 250 ms initial timer?

**Arms.** The arms differ only in the back-off cap (`retransmit_max_us`, set on host and client alike). Each flight starts at 250 ms and doubles up to the cap (`DtlsSession::next_retransmit_us`):
- `off`, today's default: 250, 500, 1000, 1000 … ms;
- `cap750`: 250, 500, 750, 750 … ms;
- `cap500`: 250, 500, 500 … ms. "A 500 ms cap" and "doubling up to 500 ms" are the same schedule.

EXP-044's `flat` arm is not repeated.

**Profiles,** with the impairment applied by each side to what it sends:
- **harsh**, as in EXP-044: 15% loss, 5% duplicates, 80 ms + U[0, 40] ms;
- **slow**: 150 ms + U[0, 50] ms, so the RTT is 300–400 ms, above the 250 ms initial timer; 1% loss; no duplicates. This profile prices needless retransmissions on a benign, slow path.

**Batches.** Every batch is 100 cycles. On each backend, each arm is used once per repetition, in a rotating order, so drift affects every arm:
- **harsh, UDP** (seeds 4701, 4702, 4703) and **harsh, GNS** (repetitions 1, 2, 3). The orders by repetition are:
  - first: `off`, `cap750`, `cap500`;
  - second: `cap750`, `cap500`, `off`;
  - third: `cap500`, `off`, `cap750`.
- **slow:** UDP seed 4704 and GNS repetition 4, in the order `off`, `cap750`, `cap500`.

That is 24 batches and 2400 reconnects, taking about 6 h. The arms are analysed as independent samples, as in EXP-044.

**Instrumentation (FB-31-02).** The DTLS trace detail of every handshake datagram, sent or received, gains two header-only fields, so no payload is logged:
- `len=<bytes>`: the datagram's size at our layer. That is the UDP payload; on GNS it excludes GNS framing and GNS's own handshake.
- `seq=<n>`: the `message_seq` of the first plaintext handshake message, when the datagram carries one.

The existing record list keeps its position, so existing parsers are unaffected.

**Per reconnect and side,** counted from the end of the stall until that side authenticated:
- bytes sent;
- bytes received;
- datagrams sent;
- **resent datagrams**: a sent datagram whose key that side already sent since the stall ended. The key is the type and `message_seq` of its first plaintext handshake message, or else its record list. It counts timer retransmissions, answers to a peer's retransmission, and restarts on a new connection;
- timer fires.

Also recorded per reconnect: the phases, `total` and the DTLS phase as in EXP-044, the process CPU, and the outcome.

**Rules, fixed now.** Each rule applies per backend, and each candidate `c` (`cap750`, `cap500`) is compared with `off`. The profile is harsh unless stated.
- **Completeness:** as EXP-044, for all 24 batches. In addition:
  - the byte and resend fields are present;
  - byte counts are non-negative integers;
  - each side's bytes sent are at least its datagrams sent;
  - resent datagrams never exceed datagrams sent.

  If any of these fails, there is no verdict.
- **Homogeneity:** each arm's 3 harsh batches, as in EXP-044. If p < 0.01, that candidate's O1 on that backend is reported per batch only. An unknown p makes it unassessable.
- **O1, P(total > 3000 ms):** `c` − `off`, with a 95% Newcombe hybrid score interval. "Better", "worse" or "no difference shown" as in EXP-044.
- **O2, p95 and p99 of total:** `c` − `off`, seeded bootstrap (43, 4000).
- **O3, p50 of total and of the DTLS phase,** on both profiles: reported only.
- **G1, bytes:** mean bytes sent per reconnect (both sides), `c` / `off`.
  - The guard fails above **1.25**, on either profile, on any backend.
  - It is compared on unrounded sums.
- **G2, robustness:** under both profiles, `c` never has more timeouts than `off`, nor more failures, nor more reconnects that never started. Each category is compared on its own.
- **G3, slow path:** on the slow profile, `c`'s p95 and p50 of total must not be "worse" than `off`'s (same bootstrap).
- **Reported:**
  - per side, bytes received, resent datagrams, timer fires and datagrams;
  - ClientHellos;
  - CPU per cycle;
  - every metric per batch.
- **Intervals** are 95% per comparison and are not adjusted for the two candidates. The reading is for the owner and is not applied automatically.
- **Reading, per candidate:**
  - **"improves the tail"** only if O1 or O2-p95 shows `c` better on at least one backend, "worse" appears on no backend for O1, O2-p95 or O2-p99, and G1, G2 and G3 hold on both backends;
  - otherwise **"not shown"**;
  - **"unassessable"** if any harsh O1 is unassessable, and **"incomplete"** without completeness.
- **What follows:**
  - If both candidates improve the tail, `cap750` keeps more of RFC 6347's back-off.
  - Choosing one, and then a soak gate run, is the owner's decision.
  - KI-35 stays open until the soak gate says otherwise.

**EXP-047 result: both candidates "not shown" (`ki35-caps-2026-10-28-005304-7fcb88c`, complete, run on `7fcb88c`, clean).** Neither cap is shown to differ from `off`, in either direction, on any predeclared outcome, and all three guards hold for both. 1800 harsh and 600 slow reconnects; no timeouts, failures or reconnects that never started; every harsh arm homogeneous (p ≥ 0.207). Re-analysing the directory gives an identical verdict.

Two earlier attempts on the same commit stopped before the end. They are kept as recorded and not judged:
- `ki35-caps-2026-10-27-103108-7fcb88c`: 3 batches and 38 cycles. The runner was a child of the agent session and ended with it.
- `ki35-caps-2026-10-27-123625-7fcb88c`: 17 batches and 52 cycles. The runner ended with no error output; the cause is not established.

| harsh | UDP off | UDP cap750 | UDP cap500 | GNS off | GNS cap750 | GNS cap500 |
|---|---|---|---|---|---|---|
| P(total > 3 s) | 19/300 | 25/300 | 15/300 | 92/300 | 82/300 | 96/300 |
| Total p50 / p95 / p99 (ms) | 1682 / 3153 / 4398 | 1682 / 3344 / 4851 | 1705 / 3142 / 3614 | 2408 / 4463 / 6516 | 2354 / 4716 / 5950 | 2518 / 4535 / 5785 |
| DTLS phase p50 / p95 / p99 (ms) | 835 / 2010 / 3715 | 858 / 2085 / 4475 | 837 / 1834 / 3239 | 812 / 2294 / 4694 | 816 / 2246 / 3014 | 810 / 2026 / 2735 |
| Bytes sent per reconnect (both sides) | 2513 | 2553 | 2520 | 2516 | 2548 | 2515 |
| Resent datagrams, client / host | 1.26 / 1.65 | 1.39 / 1.69 | 1.29 / 1.53 | 1.39 / 1.43 | 1.41 / 1.57 | 1.37 / 1.48 |
| Timer fires / ClientHellos per reconnect | 1.74 / 2.83 | 1.92 / 2.99 | 1.82 / 2.90 | 1.94 / 2.97 | 1.94 / 2.94 | 1.91 / 2.96 |

| slow (RTT 300–400 ms) | UDP off | UDP cap750 | UDP cap500 | GNS off | GNS cap750 | GNS cap500 |
|---|---|---|---|---|---|---|
| P(total > 3 s) | 0/100 | 1/100 | 0/100 | 15/100 | 23/100 | 24/100 |
| Total p50 / p95 (ms) | 1892 / 2393 | 2256 / 2425 | 1918 / 2390 | 2625 / 3126 | 2626 / 3136 | 2630 / 3164 |
| Bytes sent per reconnect | 3458 | 3458 | 3469 | 3449 | 3450 | 3444 |
| Resent datagrams, client / host | 3.02 / 3.02 | 3.03 / 3.00 | 3.04 / 3.05 | 3.02 / 2.99 | 3.02 / 3.01 | 3.00 / 3.00 |
| Timer fires per reconnect | 3.98 | 3.97 | 4.00 | 3.92 | 3.94 | 3.91 |

- **O1, P(total > 3 s), `c` − `off`:**
  - cap750: UDP [−0.023, +0.063], GNS [−0.105, +0.039];
  - cap500: UDP [−0.052, +0.025], GNS [−0.061, +0.087].
  - No difference shown in any comparison.
- **O2, `c` − `off` (ms):**
  - cap750: p95 UDP +191 [−409, +586], GNS +253 [−228, +803]; p99 UDP +453 [−2474, +2656], GNS −566 [−1465, +1719];
  - cap500: p95 UDP −11 [−760, +516], GNS +72 [−521, +661]; p99 UDP −784 [−3680, +1180], GNS −732 [−1540, +1086].
  - No difference shown.
- **O3, p50 (reported):**
  - harsh total: cap750 UDP −1 [−191, +185], GNS −54 [−231, +89]; cap500 UDP +23 [−179, +199], GNS +110 [−166, +224];
  - slow total: cap750 UDP +364 [−53, +415], GNS +1 [−19, +32]; cap500 UDP +27 [−359, +379], GNS +5 [−20, +30];
  - DTLS phase: every difference within ±23 ms, every interval includes 0.
- **Guards:**
  - G1 bytes ratio (UDP harsh, UDP slow, GNS harsh, GNS slow): cap750 1.016 / 1.000 / 1.013 / 1.001; cap500 1.003 / 1.003 / 1.000 / 0.999 (limit 1.25);
  - G2: 0 events in every category and arm;
  - G3: slow p95 and p50 no difference shown for both candidates.
- **Reported:** CPU per cycle 39–52 ms in every arm. The probe loop dominates it.
- **Reading, by the predeclared rule: cap750 "not shown", cap500 "not shown."**
  - The run does not show that a milder cap keeps EXP-044's tail gain, nor that either cap costs anything beyond the guard. The intervals allow meaningful benefits as well as harm. Of the settings tested so far, only the flat 250 ms timer showed EXP-044's gain; it stays a bounding arm, and whether the gain needs it stays unresolved.
  - Both caps passed G1. The observed mean-byte ratios are at most 1.016 (cap750: +1.6% UDP harsh, +1.3% GNS harsh). That is not proof of zero extra cost.
  - The GNS DTLS-phase p99 is lower with both caps (4694 → 3014 / 2735 ms). That is reported only, not a rule, and the total tail did not follow.
  - **Slow-path resends are similar in every arm.** Every arm fires about 4 timers and resends about 3 datagrams per side per reconnect. A hypothesis consistent with this, not established: with an RTT of 300–400 ms, the 250 ms initial timer, which no arm changed, expires before the answer arrives. The resend count also includes replies to peer retransmissions and connection restarts, so this comparison does not isolate the cause.
  - **No default changes.** KI-35 stays open. Next options for the owner: leave the back-off as is and move to the cookie round trip (FB-28-04) or the GNS establishment and session phases (EXP-041), or predeclare a study of the initial timer against the path RTT.

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
