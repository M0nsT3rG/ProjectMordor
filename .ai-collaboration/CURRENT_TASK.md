# Current task

**Task:** Set up the GitHub collaboration bridge and send ChatGPT its first review request (Phase 1 review).

**Branch:** `feature/ai-collaboration-bridge`

**Status:**

- [x] Bridge files written
- [x] `tools/collab/report.py` written and run
- [x] `CLAUDE.md` updated with the collaboration rules
- [x] Private GitHub repository created: https://github.com/M0nsT3rG/ProjectMordor (authorised by the user, 2026-10-23)
- [x] Pushed `main` and `feature/ai-collaboration-bridge`; opened PR #1 "[Project Mordor] Review — Phase 1 and Collaboration Bridge": https://github.com/M0nsT3rG/ProjectMordor/pull/1 (not merged; needs review)
- [ ] Receive ChatGPT feedback in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md), then triage it.

**Next development task once this is done (Phase 2A, no game interaction):**

1. Prototype: replace the 1 ms `Sleep` loop with waitable timers and measure the RTT overhead (R-13).
2. Prototype: add a test for the jitter input-backlog drain (gap from EXP-008).
3. Prototype: add state digests for desync detection.

D-007 (legal) and D-008 (target platform) stay with the user. No game-side work happens until gate G0 passes.
