# Current task

**Last completed:** review #6 follow-up (FB-6-01 to FB-6-05):
- DTLS 1.2 + RFC 7250 over `Transport::Unreliable` on both backends, with a fail-closed invitation binding;
- KI-27 root-caused and mitigated;
- wording sweep;
- separate-process asymmetric scenarios;
- bounded `--connect-deadline-s`.

See EXP-018, [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md) and `docs/d013-evaluation.md`.

**Branches and PRs** (all open, none merged; merging needs the user):

| PR | Branch | Base | Content |
|---|---|---|---|
| [#1](https://github.com/M0nsT3rG/ProjectMordor/pull/1) | `feature/ai-collaboration-bridge` | `main` | Phase 1 + collaboration bridge |
| [#2](https://github.com/M0nsT3rG/ProjectMordor/pull/2) | `feature/realtime-soak` | #1 | Real-time soak, gates v1–v3 |
| [#3](https://github.com/M0nsT3rG/ProjectMordor/pull/3) | `feature/gns-transport` | #2 | GNS spike (D-012) |
| [#4](https://github.com/M0nsT3rG/ProjectMordor/pull/4) | `feature/transport-interface` | #3 | Transport contract v2 + UDP backend |
| [#5](https://github.com/M0nsT3rG/ProjectMordor/pull/5) | `feature/gns-adapter` | #4 | GNS adapter, per-transport tests, gate v4, D-013 evaluation |
| [#6](https://github.com/M0nsT3rG/ProjectMordor/pull/6) | `feature/gns-hardening` | #5 | Review #5: counters, deadlines, impairment guard, GNS substitute tests, DTLS proof |
| #7 | `feature/dtls-transport` | #6 | Review #6: DTLS over the Transport + binding, KI-27, asymmetric processes |

Local only: `feature/reliable-events` (in-house ReliableChannel WIP, not pushed).

**Repository:** https://github.com/M0nsT3rG/ProjectMordor (private)

## Waiting on the user

- **D-013:** DTLS works over a real transport on both backends. Still needed: the owner's go-ahead to carry **game traffic** over it, plus an independent review. Until authentication is implemented and reviewed: loopback or trusted lab networks only.
- **Gate G0:** D-007 (legal) and D-008 (target platform).
- **LAN test:** approval for a temporary firewall rule.

## Next tasks (no user input needed)

1. Reliable game events on `ReliableOrdered` (GNS), with application ids, acks and replay (KI-28).
2. A cookie before any per-peer DTLS state (KI-29).
3. Report the KI-27 sanity-check race upstream (needs the owner: it means posting publicly).
4. **FB-2-05:** wider stall coverage; repeated soak runs to characterise recovery variance (KI-18).
