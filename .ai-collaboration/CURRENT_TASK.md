# Current task

**Last completed:** review #10 follow-up (FB-10-01 to FB-10-05, answer 5) on `feature/ki35-ki37-investigation`. Record: EXP-023.
- KI-37 and KI-30 resolved; protocol v3 (explicit leave); soak gate v6.
- KI-35 mechanisms shown and partly fixed; harsh-gns-secure still fails recovery (KI-35, KI-38). v6 soaks: 11/12.

**Previously:** review #9 follow-up (FB-9-01 to FB-9-05):
- protocol v2 application-level reliable events (KI-28 resolved, with limits);
- soak gate v5 (realized impairment);
- authenticated-only liveness;
- key-file hardening;
- handshake rate limiting.

v5 soaks passed 10 of 12: KI-35 and KI-37 are open. See EXP-022 and [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md).

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
| [#8](https://github.com/M0nsT3rG/ProjectMordor/pull/8) | `feature/secure-transport` | #7 | Review #7: secure transport decorator, bounded pending state, timer policy, KI-27 reproducer |
| [#9](https://github.com/M0nsT3rG/ProjectMordor/pull/9) | `feature/secure-provisioning` | #8 | Review #8: secure executables, secure reliable channel, cookie-aware pending, idle timeout, KI-27 upstream |
| [#10](https://github.com/M0nsT3rG/ProjectMordor/pull/10) | `feature/secure-liveness` | #9 | Review #9: authenticated liveness, protocol v2 application events, soak gate v5, key-file hardening |
| #11 | `feature/ki35-ki37-investigation` | #10 | Review #10: event-ack bounds, stateless cookie gate, protocol v3 leave handshake, KI-35 traces and fixes, soak gate v6 |

Local only: `feature/reliable-events` (in-house ReliableChannel WIP, not pushed).

**Repository:** https://github.com/M0nsT3rG/ProjectMordor (private)

## Waiting on the user

- **D-013:** the executables run over DTLS with provisioned keys (D-016). Still needed:
  - a stateless cookie (KI-30);
  - an independent review;
  - a LAN test. Until authentication is implemented and reviewed: loopback or trusted lab networks only.
- **Gate G0:** D-007 (legal) and D-008 (target platform).
- **LAN test:** approval for a temporary firewall rule.

## Next tasks (no user input needed)

1. Root-cause **KI-37** (lost final leave after a GNS reconnect) and **KI-35** (secure reconnect tail).
2. A cookie before any per-peer DTLS state (KI-29).
3. **KI-27:** follow ValveSoftware/GameNetworkingSockets#458.
4. **FB-2-05:** wider stall coverage; repeated soak runs to characterise recovery variance (KI-18).
