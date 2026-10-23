# Known issues and limitations

| ID | Issue | Severity | Source | Status |
|---|---|---|---|---|
| KI-01 | Gate G0: Steam-build protection plus the EULA block every game-side milestone | Blocker | risk R-01, R-02 | Waiting on user decisions D-007 and D-008 |
| KI-02 | Real-time RTT is inflated by about 30–40 ms by the 1 ms `Sleep` main loop | Low | EXP-005, R-13 | Open |
| KI-03 | No dedicated test covers the host input-backlog drain under jitter | Low | EXP-008 | Open |
| KI-04 | The prototype has no authentication or encryption; the reconnect token is not a credential | Medium (if exposed) | docs/protocol.md, R-12 | Accepted for loopback and LAN |
| KI-05 | IPv4 only; interpolation ignores 32-bit tick wraparound | Low | `shortcut:` comments in the code | Accepted |
| KI-06 | The LAN test has not been run | Medium | EXP-007 | Needs a second PC and firewall approval |
| KI-07 | Default port 47800 is occupied on the dev machine | Low | docs/environment.md | Use `--port` |
| KI-08 | Fresh executables can start late on first launch; the cause is unconfirmed | Low | EXP-006 | Mitigated in `test.ps1` |
| KI-09 | The Codex CLI review fails with `CryptUnprotectData failed: 2148073483` | Process | EXP-008 | Open: an independent agent reviewer is used instead |
| KI-10 | The `heavy_loss_and_duplication_never_crash` test is a smoke test only | Low | EXP-008 | Accepted |
