# Known issues and limitations

| ID | Issue | Severity | Source | Status |
|---|---|---|---|---|
| KI-01 | Gate G0: Steam-build protection plus the EULA block every game-side milestone | Blocker | risk R-01, R-02 | Waiting on user decisions D-007 and D-008 |
| KI-02 | ~~Real-time RTT inflated by the 1 ms `Sleep` main loop~~ (measured about 50 ms on lan). Fixed with a per-thread high-resolution waitable timer; lan p50 went from 55.7 to 9.8 ms | — | EXP-005, EXP-010 | **Resolved 2026-10-23** |
| KI-03 | ~~No test covers the host input-backlog drain~~. Fixed: the backlog is now shed, not simulated, with 3 regression tests | — | EXP-008, EXP-009, FB-1-01 | **Resolved 2026-10-23** |
| KI-04 | No authentication or encryption. The reconnect token is a 64-bit non-cryptographic value: whoever holds it can take over the slot from any address (tested: `reconnect_token_migrates_slot_to_new_endpoint`). No rate limiting. | Medium (if exposed) | docs/protocol.md, R-12, FB-1-02 | Accepted for loopback/LAN; must be resolved before any WAN use |
| KI-05 | IPv4 only; interpolation ignores 32-bit tick wraparound | Low | `shortcut:` comments in the code | Accepted |
| KI-06 | The LAN test has not been run | Medium | EXP-007 | Needs a second PC and firewall approval |
| KI-07 | Default port 47800 is occupied on the dev machine | Low | docs/environment.md | Use `--port` |
| KI-08 | Right after a relink, the first E2E run exchanged **no packets**. Cause unknown and environmental: not a late start, and no firewall rules exist. A warm-up run avoids it. | Low | EXP-006 | Workaround in `test.ps1` |
| KI-09 | The Codex CLI review fails with `CryptUnprotectData failed: 2148073483` | Process | EXP-008 | Open: an independent reviewer agent is used instead |
| KI-10 | `heavy_loss_and_duplication_never_crash` is a smoke test only | Low | EXP-008 | Accepted |
| KI-11 | The grace period (30 s) is longer than the client reconnect window (15 s). A client that gives up leaves its slot reserved for up to about 15 s more, and new joins get `ServerFull` meanwhile. | Low | FB-1-07 | Accepted trade-off; user-initiated retry deferred |
| KI-12 | ~~After a stall or one-way loss the client believes it is connected while the host has timed it out~~. Confirmed by the soak: 3085 ms recovery. Fixed: the host sends `Disconnect{Timeout}` to grace-slot endpoints and the client reconnects immediately; recovery is now 19–572 ms | — | EXP-009, EXP-010 | **Resolved 2026-10-23** |
| KI-13 | There is no reliable, ordered event channel. Every message is unreliable, which is fine for movement but not for combat results or Nemesis effects. | Design | FB-1-04, D-010 | Deferred (P2) |
| KI-14 | Backlog shedding discards movement after jitter bursts. The local player sees a small reconciliation correction. | Low | FB-1-01, EXP-009, EXP-010 | Accepted. Measured in the soak: the harsh profile shed 72 inputs in 120 s and the largest correction was 0.33 units (2 timesteps) |
| KI-15 | RTT max for a stalled client includes the stall duration: a heartbeat sent before the stall is answered after it (for example 5164 ms in harsh). p99 is not affected. | Low (measurement) | EXP-010 | Accepted; could drop samples that span a stall |
| KI-16 | The soak covers loopback only, 120 s runs and one simulated (in-process) stall. Missing: a real NIC, multi-hour runs, OS-level suspension, host stalls, repeated or asymmetric outages, startup races, repeated reconnects within the grace period. | Medium | docs/soak-testing.md, FB-2-05 | Open (P1, next soak iteration) |
| KI-17 | `Disconnect{Timeout}` is unauthenticated. Delayed and replayed notices are now ignored (sequence guard), but a forged packet with a newer sequence can still force a reconnect, which the token then restores. | Medium (if exposed) | FB-2-02, KI-04 | Accepted for loopback/trusted LAN; needs an authenticated transport before WAN |
| KI-18 | Harsh-profile stall recovery varies between runs (572 ms and 1567 ms, both under the 3000 ms gate). Not characterised statistically. | Low | EXP-010, EXP-011 | Open: measure the distribution over repeated runs |
| KI-21 | GNS 1.6.1 open-source accepts unauthenticated peers on any IP by default (`IP_AllowWithoutAuth=2`). GNS native certificate trust is internal-only. | High (if exposed) | EXP-014, `docs/authentication-design.md` | Open: app-layer authentication pending D-013 |
| KI-22 | The GNS adapter is not implemented. Host and client run only on the UDP backend, so the session, reconnect and soak tests have not run on GNS. | Medium | FB-3-02 | Next milestone |
| KI-20 | No burst or peak bandwidth gate. 1 s window peaks exceed the mean-rate ceiling under jitter and duplication (EXP-012), so a peak gate needs its own derived bound. | Low | Review #2 follow-up, EXP-012 | Deferred: future gate version |
| KI-19 | No gate on shed inputs or queue depth yet: there is no design-derived bound. | Low | FB-2-03 | Deferred: needs repeated baselines |
