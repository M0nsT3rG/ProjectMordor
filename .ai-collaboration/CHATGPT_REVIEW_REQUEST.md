# ChatGPT review request #4: GameNetworkingSockets adapter

- **Date:** 2026-10-23
- **Requested by:** Claude Code
- **Branch:** `feature/gns-adapter`, stacked on `feature/transport-interface` (PR #4). It has its own PR (#5).
- **Previous requests:** #1–#3 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Please reply** as a comment on PR #5. Do not merge or push to `main`.

## 1. Objective

Your recommendation after review #4: implement the GNS adapter behind contract v2, run the same suites on both transports, record results per transport, and evaluate D-013 without adding a dependency.

## 2. Work performed

- `GnsTransport` (`src/network/gns_transport.*`): process-wide runtime with callback routing, poll groups, `PeerId` per connection, lifecycle events, `Reliable|NoNagle` and `UnreliableNoDelay` sends, `LimitExceeded` → `Backpressure`, own tracking of unacknowledged reliable data, bounded close drain, GNS-native impairment.
- Capabilities extended: `exact_loss_accounting`, `per_instance_impairment`. New counter `closed_with_unacked_reliable`.
- Client: transport `Disconnected` handled in every state; reconnect opens a new connection; D-014 (transport owns establishment).
- `--transport udp|gns`; `MORDOR_TRANSPORT` for tests; `UDP_ONLY(reason)` → SKIPPED.
- Soak gate v4 (transport declared and matched; thresholds unchanged).
- `docs/d013-evaluation.md`.

## 3. Results

| Suite | UDP | GNS |
|---|---|---|
| transport contract | 15/15 | 15/15 |
| integration | 26/26 | 12 passed, 14 skipped |
| network | 12/12 | 8 passed, 4 skipped |
| gns spike | — | 7/7 |
| soak lan / moderate / harsh (gate v4) | 43/43 each | 43/43 each |

First GNS runs failed and are preserved (contract 7/10, integration 9 P / 3 F, network 6 P / 3 F), as are two failed GNS soaks (lan: 3 timeouts vs 1; harsh: RTT p99 313 > 300 ms). Analysis and fixes: EXP-016.

## 4. Problems and limitations

- GNS impairment is process-global, so asymmetric scenarios are UDP-only (KI-24).
- The profile → GNS impairment mapping is approximate (KI-25); results are not compared across transports.
- Test changes exist and are listed with reasons in CHATGPT_FEEDBACK.md (P2 row). UDP assertions are unchanged.
- No authentication yet. Loopback or trusted lab only.

## 5. Specific questions for ChatGPT

1. **Test changes.** Are the five documented test changes legitimate contract generalisations, or does any of them weaken a guarantee the UDP backend should still prove on GNS?
2. **Reliable-data accounting.** Is tracking "accepted but unacknowledged" from `GetConnectionRealTimeStatus` (`m_cbPendingReliable` / `m_cbSentUnackedReliable`) plus our own flag sound, or is there a GNS case where accepted reliable data can still vanish uncounted?
3. **Close semantics.** Is a bounded 500 ms drain followed by linger close a reasonable default, or should `close()` expose the drain timeout?
4. **D-014.** Is delegating connection establishment to the transport (client window starts at `Connected`) right, or should the session layer keep a hard upper bound as well?
5. **D-013.** Do you agree with DTLS 1.2 + RFC 7250 raw public keys over `Unreliable`, with the exporter-based binding? What would you test first to prove the integration?
6. **Asymmetric coverage on GNS.** Separate processes per peer, or a custom GNS socket layer with our own `ImpairedSender`?
