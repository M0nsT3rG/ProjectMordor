# Current task

**Last completed:** triage and fixes for ChatGPT review #2 (FB-2-01 to FB-2-06), with a soak re-run under gate v2. See [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md) and EXP-011.

**Branches and PRs:**

- `feature/ai-collaboration-bridge`: PR [#1](https://github.com/M0nsT3rG/ProjectMordor/pull/1), open, waiting for the user to merge.
- `feature/realtime-soak`: PR [#2](https://github.com/M0nsT3rG/ProjectMordor/pull/2), stacked on #1, open. Review #2 is triaged.
- `feature/reliable-events`: **local only, not pushed.** WIP commit `187a121`: a minimal in-house `ReliableChannel` plus protocol v2 Event/EventAck, 21 passing unit tests. Host integration is unfinished and the branch does not build.

**Repository:** https://github.com/M0nsT3rG/ProjectMordor (private)

## Waiting on the user

- **Transport for the reliable event channel** (review #2, answer 4): ENet (MIT) or GameNetworkingSockets (BSD-3) behind an interface, compared with the parked in-house channel using the same tests. Adding a third-party library needs the user's approval.
- **Gate G0:** D-007 (legal) and D-008 (target platform).
- **LAN test:** approval for a temporary firewall rule.

## Next tasks without user input (Phase 2A, no game interaction)

1. **P1 (FB-2-05):** wider stall coverage. Host stall, repeated and asymmetric outages, repeated reconnects within the grace period, seeded randomized schedules, longer runs (30–60 min).
2. **P1 (KI-18):** repeat each profile N times; report the distribution of recovery time and RTT.
3. **P2:** desync digests.
