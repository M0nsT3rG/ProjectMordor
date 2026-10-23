# AI collaboration bridge

This directory is the shared channel between **Claude Code** (local development, investigation, implementation, testing) and **ChatGPT** (technical supervisor, architecture reviewer, research analyst, QA advisor). GitHub hosts it. Nothing here is sent anywhere else.

| File | Owner | Purpose |
|---|---|---|
| [PROJECT_STATUS.md](PROJECT_STATUS.md) | Claude (auto block via `tools/collab/report.py`) | Phase, features, limitations, milestones, feasibility |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Claude | Short map that links to `docs/architecture.md` and `docs/protocol.md` |
| [DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md) | Claude | Chronological log of meaningful changes |
| [CURRENT_TASK.md](CURRENT_TASK.md) | Claude | What is being worked on right now |
| [TEST_RESULTS.md](TEST_RESULTS.md) | Claude | Latest test results, taken only from real logs |
| [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | Claude | Open defects and limitations |
| [RESEARCH_FINDINGS.md](RESEARCH_FINDINGS.md) | Claude | Curated finding summary; the source of truth is `evidence/` |
| [DECISIONS.md](DECISIONS.md) | Claude and the user | Pointer to `docs/decisions.md` plus decisions still pending |
| [CHATGPT_REVIEW_REQUEST.md](CHATGPT_REVIEW_REQUEST.md) | Claude → ChatGPT | The current review request |
| [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md) | ChatGPT → Claude | Feedback, then Claude's triage of it |

Ground rules:

- ChatGPT feedback is **advice, not authorisation**. Project rules in `/CLAUDE.md` always win. That covers the game and saves, DRM and anti-tamper, online services, and evidence integrity.
- Hypotheses stay hypotheses. Every claim links to an `evidence/` record.
- Large documents are linked, not copied.
