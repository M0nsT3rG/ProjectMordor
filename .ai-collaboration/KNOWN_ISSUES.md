# Known issues and limitations

| ID | Issue | Severity | Source | Status |
|---|---|---|---|---|
| KI-01 | Gate G0: Steam-build protection plus the EULA block every game-side milestone | Blocker | risk R-01, R-02 | Waiting on user decisions D-007 and D-008 |
| KI-02 | Real-time RTT is inflated by about 30–40 ms by the 1 ms `Sleep` main loop | Low | EXP-005, R-13 | Open (Phase 2A P2) |
| KI-03 | ~~No test covers the host input-backlog drain~~. Fixed: the backlog is now shed, not simulated, with 3 regression tests | — | EXP-008, EXP-009, FB-1-01 | **Resolved 2026-10-23** |
| KI-04 | No authentication or encryption. The reconnect token is a 64-bit non-cryptographic value: whoever holds it can take over the slot from any address (tested: `reconnect_token_migrates_slot_to_new_endpoint`). No rate limiting. | Medium (if exposed) | docs/protocol.md, R-12, FB-1-02 | Accepted for loopback/LAN; must be resolved before any WAN use |
| KI-05 | IPv4 only; interpolation ignores 32-bit tick wraparound | Low | `shortcut:` comments in the code | Accepted |
| KI-06 | The LAN test has not been run | Medium | EXP-007 | Needs a second PC and firewall approval |
| KI-07 | Default port 47800 is occupied on the dev machine | Low | docs/environment.md | Use `--port` |
| KI-08 | Right after a relink, the first E2E run exchanged **no packets**. Cause unknown and environmental: not a late start, and no firewall rules exist. A warm-up run avoids it. | Low | EXP-006 | Workaround in `test.ps1` |
| KI-09 | The Codex CLI review fails with `CryptUnprotectData failed: 2148073483` | Process | EXP-008 | Open: an independent reviewer agent is used instead |
| KI-10 | `heavy_loss_and_duplication_never_crash` is a smoke test only | Low | EXP-008 | Accepted |
| KI-11 | The grace period (30 s) is longer than the client reconnect window (15 s). A client that gives up leaves its slot reserved for up to about 15 s more, and new joins get `ServerFull` meanwhile. | Low | FB-1-07 | Accepted trade-off; user-initiated retry deferred |
| KI-12 | One-way loss (uplink only): the client still receives snapshots and stays `Connected` while the host times it out. It recovers about 3 s after the host stops sending (the client times out, then rejoins). A host-side "you are disconnected" reply would be faster. | Low | EXP-009 (`reconnect_after_grace_expiry_gets_new_identity`) | Open (P1) |
| KI-13 | There is no reliable, ordered event channel. Every message is unreliable, which is fine for movement but not for combat results or Nemesis effects. | Design | FB-1-04, D-010 | Deferred (P2) |
| KI-14 | Backlog shedding discards movement after jitter bursts. The local player sees a small reconciliation correction. | Low | FB-1-01, EXP-009 | Accepted (standard trade-off); measure in real-time soak |
