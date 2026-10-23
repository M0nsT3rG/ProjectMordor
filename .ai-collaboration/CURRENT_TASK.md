# Current task

**Last completed:** GameNetworkingSockets adapter behind Transport contract v2 (ChatGPT recommendation after review #4), the same suites run on both transports, soak gate v4, and the D-013 evaluation. See EXP-016, [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md), `docs/transport-interface.md` and `docs/d013-evaluation.md`.

**Branches and PRs** (all open, none merged; merging needs the user):

| PR | Branch | Base | Content |
|---|---|---|---|
| [#1](https://github.com/M0nsT3rG/ProjectMordor/pull/1) | `feature/ai-collaboration-bridge` | `main` | Phase 1 + collaboration bridge |
| [#2](https://github.com/M0nsT3rG/ProjectMordor/pull/2) | `feature/realtime-soak` | #1 | Real-time soak, gates v1–v3 |
| [#3](https://github.com/M0nsT3rG/ProjectMordor/pull/3) | `feature/gns-transport` | #2 | GNS spike (D-012) |
| [#4](https://github.com/M0nsT3rG/ProjectMordor/pull/4) | `feature/transport-interface` | #3 | Transport contract v2 + UDP backend |
| #5 | `feature/gns-adapter` | #4 | GNS adapter, per-transport tests, gate v4, D-013 evaluation |

Local only: `feature/reliable-events` (in-house ReliableChannel WIP, not pushed).

**Repository:** https://github.com/M0nsT3rG/ProjectMordor (private)

## Waiting on the user

- **D-013:** approve (or reject) the recommendation: DTLS 1.2 via OpenSSL with pinned RFC 7250 raw public keys; fallback Noise via snow. No crypto dependency is added until then. Until authentication is implemented and reviewed: loopback or trusted lab networks only.
- **Gate G0:** D-007 (legal) and D-008 (target platform).
- **LAN test:** approval for a temporary firewall rule.

## Next tasks (no user input needed)

1. Reliable game events on `ReliableOrdered` (GNS), with the in-house channel as an oracle if useful.
2. Asymmetric-impairment coverage on GNS using separate processes (KI-24).
3. **FB-2-05:** wider stall coverage; repeated soak runs to characterise recovery variance (KI-18).
