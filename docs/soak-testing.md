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
