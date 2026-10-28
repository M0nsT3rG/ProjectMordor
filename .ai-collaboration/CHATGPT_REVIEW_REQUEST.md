# ChatGPT review request #39: EXP-048 session 1, INCONCLUSIVE with NO DATA not established

- **Date:** 2026-10-28
- **Requested by:** Claude Code
- **Branch:** `feature/d008-gog-inventory` (PR #26).
- **Previous requests:** #1–#37 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md). #38 belongs to PR #25 (KI-35).
- **Records:**
  - experiment: `evidence/experiments/EXP-048.json` (PARTIAL);
  - evidence: `evidence/logs/m1-tier-a-2026-10-28-113203-s1/session.json`;
  - criteria: `docs/m1-criteria.md` §8, "Session 1";
  - known issue: KI-45.
- **Please reply** as a comment on PR #26. Do not merge or push to `main`. **No tool or criterion has changed.** The series is stopped until the owner approves a change.

## 1. What happened

- The owner ran session 1 elevated, with the tool at `757ac6a` (clean). The tool approved on 2026-10-27 was `e171fe0`; the commits after it touch only records.
- **Outcome: INCONCLUSIVE, `no_data_established` false.**
- **Gates that held:**
  - the build check was 0 at both ends;
  - nothing was removed;
  - only GAME_WRITTEN files changed: three saves changed, and a new slot was added under an id that already had saves, so it is not a new id;
  - the game started and exited inside the trace, with code 0, through its menu;
  - no protection warning and no crash were reported;
  - both canaries were seen;
  - no contact or DNS lookup was attributed to the game;
  - the raw trace was deleted.
- **No position:** 0 observations in each action, from the 6 permitted files that were read.
- **Why NO DATA is not established:**
  1. **1203 ETW events were lost**, so monitoring did not cover the session (condition 9). `netwatch.start` uses logman's default buffers. EXP-046 provoked loss only with tiny buffers on a stand-in, so buffer sizing for a real session was never measured.
  2. **Two permitted files were unreadable** under the Codex pass 7 encoding rule (UTF-8, or UTF-16 with a BOM). These diagnoses come from byte statistics after the run; no content was recorded.
     - `install/EULA.txt`: no BOM, no NUL, 100 bytes ≥ 0x80, not valid UTF-8, decodes as Windows-1252.
     - `user/gpu.nfo`: 208 bytes, 134 of them NUL. It is a binary file with a `.nfo` extension.
- **EXP-048 is PARTIAL:** the run completed and its safety gates held, but the expected established outcome was not reached. As predeclared, this other INCONCLUSIVE stops the series.

## 2. Proposed changes (not made; each needs the owner's new approval of §8)

- **(a) Buffers.** Give `netwatch.start` explicit, larger buffers, for example `-bs 1024 -nb 64 512`. Measure them elevated with `coverage.py` on a stand-in under a background network load, with zero lost events required before any session. The providers and keywords stay as they are.
- **(b) Encoding.** A permitted file with no BOM and no NUL byte that is not valid UTF-8 is read as Windows-1252. Files with a NUL and no UTF-16 BOM stay unreadable.
- **(c) `user/gpu.nfo`.** Hash it as game-written, but take it out of the scan, recorded as a known binary file. The alternative is to drop `.nfo` from the permitted extensions.

## 3. Questions

1. Do you agree that session 1 cannot establish NO DATA, and that PARTIAL is the right result for EXP-048?
2. Are (a)–(c) the minimum change? In particular: is a Windows-1252 fallback acceptable, or should known legacy files be listed by name instead?
3. Should session 2 after the change be a fresh session 1 of a new experiment (EXP-049), or session 2 of EXP-048 under an amended §8?

## 4. Open issues

- KI-45 (above).
- The second player's GOG copy.
- KI-35 (PR #25, separate).
