# Current task

**Last completed:** triage of ChatGPT review #3 (FB-3-01 to FB-3-06), plus the `Transport` interface with the UDP backend and the authenticated-peer design. See [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md), EXP-014 and `docs/authentication-design.md`.

**Branches and PRs** (all open, none merged; merging needs the user):

| PR | Branch | Base | Content |
|---|---|---|---|
| [#1](https://github.com/M0nsT3rG/ProjectMordor/pull/1) | `feature/ai-collaboration-bridge` | `main` | Phase 1 + collaboration bridge |
| [#2](https://github.com/M0nsT3rG/ProjectMordor/pull/2) | `feature/realtime-soak` | #1 | Real-time soak, gates v1–v3 |
| [#3](https://github.com/M0nsT3rG/ProjectMordor/pull/3) | `feature/gns-transport` | #2 | GNS spike (D-012) |
| #4 | `feature/transport-interface` | #3 | Transport interface + UDP backend, review #3 triage, auth design |

Local only: `feature/reliable-events` (in-house ReliableChannel WIP, not pushed).

**Repository:** https://github.com/M0nsT3rG/ProjectMordor (private)

## Waiting on the user

- **D-013:** crypto library for authenticated peers. libsodium (ISC) is proposed; an audited Noise implementation is the alternative.
- **Gate G0:** D-007 (legal) and D-008 (target platform).
- **LAN test:** approval for a temporary firewall rule.

## Next tasks (no user input needed)

1. **GNS adapter (KI-22):**
   - `GnsTransport` behind the interface;
   - GNS owns liveness and reconnect;
   - native fault injection and transport-labelled gates;
   - run the integration, network and soak suites on GNS, with UDP-only tests marked SKIP rather than passed;
   - disconnect-mid-backlog and reconnect tests (FB-3-03 remainder).
2. **FB-2-05:** wider stall coverage.
