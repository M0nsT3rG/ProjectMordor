# Current task

**Last completed:** review #7 follow-up (FB-7-01 to FB-7-05):
- `SecureTransport` decorator (D-015): Host/Client traffic over DTLS with no plaintext fallback;
- bounded pre-authentication state;
- DTLS timer policy;
- binding robustness;
- KI-27 reproducer and upstream draft (not published).

See EXP-019, [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md) and `docs/d013-evaluation.md`.

**Branches and PRs** (all open, none merged; merging needs the user):

| PR | Branch | Base | Content |
|---|---|---|---|
| [#1](https://github.com/M0nsT3rG/ProjectMordor/pull/1) | `feature/ai-collaboration-bridge` | `main` | Phase 1 + collaboration bridge |
| [#2](https://github.com/M0nsT3rG/ProjectMordor/pull/2) | `feature/realtime-soak` | #1 | Real-time soak, gates v1–v3 |
| [#3](https://github.com/M0nsT3rG/ProjectMordor/pull/3) | `feature/gns-transport` | #2 | GNS spike (D-012) |
| [#4](https://github.com/M0nsT3rG/ProjectMordor/pull/4) | `feature/transport-interface` | #3 | Transport contract v2 + UDP backend |
| [#5](https://github.com/M0nsT3rG/ProjectMordor/pull/5) | `feature/gns-adapter` | #4 | GNS adapter, per-transport tests, gate v4, D-013 evaluation |
| [#6](https://github.com/M0nsT3rG/ProjectMordor/pull/6) | `feature/gns-hardening` | #5 | Review #5: counters, deadlines, impairment guard, GNS substitute tests, DTLS proof |
| [#7](https://github.com/M0nsT3rG/ProjectMordor/pull/7) | `feature/dtls-transport` | #6 | Review #6: DTLS over the Transport + binding, KI-27, asymmetric processes |
| #8 | `feature/secure-transport` | #7 | Review #7: secure transport decorator, bounded pending state, timer policy, KI-27 reproducer |

Local only: `feature/reliable-events` (in-house ReliableChannel WIP, not pushed).

**Repository:** https://github.com/M0nsT3rG/ProjectMordor (private)

## Waiting on the user

- **D-013:** game traffic runs over DTLS in-process (decorator). Still needed:
  - key/invitation provisioning for the executables (KI-31);
  - a stateless cookie (KI-30);
  - an independent review. Until authentication is implemented and reviewed: loopback or trusted lab networks only.
- **Gate G0:** D-007 (legal) and D-008 (target platform).
- **LAN test:** approval for a temporary firewall rule.

## Next tasks (no user input needed)

1. Reliable game events on `ReliableOrdered` (GNS), with application ids, acks and replay (KI-28).
2. A cookie before any per-peer DTLS state (KI-29).
3. **KI-27 upstream report:** the draft `docs/upstream/gns-ki27-issue.md` awaits the owner's review and posting.
4. **FB-2-05:** wider stall coverage; repeated soak runs to characterise recovery variance (KI-18).
