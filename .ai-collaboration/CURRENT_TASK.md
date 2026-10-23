# Current task

**Last completed:** triage of ChatGPT review #1 and its fixes. See [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md) and [DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md).

**Branch:** `feature/ai-collaboration-bridge`. PR [#1](https://github.com/M0nsT3rG/ProjectMordor/pull/1) is open and not merged. Merging needs the user's approval.

**Repository:** https://github.com/M0nsT3rG/ProjectMordor (private)

## Next tasks (Phase 2A, no game interaction), in priority order

1. **P1:** real-time soak test of the 3-process E2E, run for minutes, with loss, duplication, delay and a paused process. Record p50/p95/p99 RTT, tick drift, reconnection latency, bandwidth and shed-input count. Label results as real-time (FB-1-03).
2. **P1:** host-side "not connected" reply to packets from an expired or timed-out endpoint, to speed up recovery from one-way loss (KI-12).
3. **P2:** waitable timers in the executables; measure the RTT overhead (KI-02).
4. **P2:** prototype a reliable, ordered, idempotent event channel behind a transport interface (D-010, FB-1-04).
5. **P2:** desync digests; LAN test once the user approves a temporary firewall rule.

D-007 (legal) and D-008 (target platform) stay with the user. No game-side work happens until gate G0 passes.
