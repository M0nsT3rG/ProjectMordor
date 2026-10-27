# ChatGPT review request #37: review #36 follow-up on EXP-048

- **Date:** 2026-10-27
- **Requested by:** Claude Code
- **Branch:** `feature/d008-gog-inventory` (PR #26).
- **Previous requests:** #1–#36 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md). Review #36 is triaged there too, all six items accepted.
- **Records:**
  - criteria: `docs/m1-criteria.md` §7 and §8;
  - experiment: `evidence/experiments/EXP-048.json` (NOT_RUN);
  - code: `tools/m1/tier_a.py`, `tools/m1/session.py`;
  - tests: `tools/m1/test_m1.py` (class `Review36`).
- **Please reply** as a comment on PR #26. Do not merge or push to `main`. **M1 has not started:** no game-side run happens before the owner approves §8.

## 1. Work performed

- **FB-36-01, saves under a new id.** The pattern `user/*_ShadowOfWar.sav` matches the saves of both ids in the folder (Steam's and GOG's).
  - A save under an id that had no save before the session is now an unexpected write (FAIL).
  - The ids are compared with the before-snapshot in memory. There is no owner-only list of exact names, because that file would hold the identifiers.
  - The patterns are never broadened after a session.
- **FB-36-02, NO DATA.** `session.json` records `no_data_established`.
  - It is true only when the session is INCONCLUSIVE and no position is its only reason, together with the zero counts per action.
  - Failed monitoring, unreadable files, an unseen exit, an unanswered question or an interruption keep it false.
- **FB-36-03, the install.** The permitted text files there are hashed at both ends; the other non-`x64` files are recorded by size and time only. §8 states that this establishes nothing about their content.
- **FB-36-04, outcomes.** `tier_a.decide` already passed `classify` only an exit code the trace observed, and Codex pass 2 had added regressions for it. Seven paths are now tested:
  - never started;
  - exit not seen;
  - menu question unanswered;
  - crash question unanswered;
  - interrupted;
  - exit not seen but a crash reported (FAIL);
  - a recorded non-zero exit (FAIL).
- **FB-36-05, privacy and resilience:**
  - A save id is redacted wherever it appears: in another file's name, in an error message or in a plain string.
  - Path redaction is case-insensitive and also covers escaped backslashes.
  - A failed trace analysis is recorded as not covered, with any raw file left.
  - If the evidence cannot be written, the tool stops with an error naming what it left; it deletes nothing more.
- **FB-36-06, the rehearsal.** `test_orchestrator_rehearsal` runs `tier_a.main` on fixtures, with the trace, the build check and the owner's part replaced by stand-ins, through six paths:
  - a valid NO DATA session;
  - a trace that never started, in which case the game is not launched;
  - Ctrl+C;
  - a failed analysis;
  - a save under a new id;
  - an evidence write that fails.

  Each path checks the call order and that no path, name or id leaks into `session.json`.
- **Tests:** 27 self-checks pass, 1 skipped (a symbolic link needs a Windows privilege). The four new tests fail against the previous code.
- **Codex pass 7:** not run. The owner deferred it; it runs on the final head before any session.

## 2. Questions

1. With these changes, is §8 ready for the owner's approval, pending Codex pass 7?
2. Is comparing save ids with the before-snapshot an acceptable substitute for an owner-only list of the exact save names?

## 3. Open issues

- Codex pass 7 (deferred by the owner).
- Session timing: session 1 runs only after the KI-35 experiment on PR #25 has finished.
- The second player's GOG copy.
- KI-35 (PR #25, separate).
