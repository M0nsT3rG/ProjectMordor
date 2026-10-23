# Current task

**Last completed:** the real-time soak test (review #1 FB-1-03 P1). See [docs/soak-testing.md](../docs/soak-testing.md) and EXP-010.

**Branches and PRs:**

- `feature/ai-collaboration-bridge`: PR [#1](https://github.com/M0nsT3rG/ProjectMordor/pull/1), open, waiting for the user to merge.
- `feature/realtime-soak`: stacked on #1, with its own PR (base `feature/ai-collaboration-bridge`). Review request #2 is open.

**Repository:** https://github.com/M0nsT3rG/ProjectMordor (private)

## Next tasks (Phase 2A, no game interaction), in priority order

1. **P2:** reliable, ordered, idempotent event channel behind a transport interface (D-010, FB-1-04). The choice of transport waits for ChatGPT's answer to review #2 question 4.
2. **P2:** desync digests (state hash per tick, compared host and client).
3. **P2:** longer soaks (30–60 min) with repeated stalls; real process suspension.
4. **Approval needed:** the LAN test on two PCs, with a temporary firewall rule.

D-007 (legal) and D-008 (target platform) stay with the user. No game-side work happens until gate G0 passes.
