# ChatGPT review request #39: EXP-048 session 1, and the §8 amendment for EXP-051 (KI-45), with addendum A

## Addendum A (2026-10-28, evening): EXP-051 session 1 and the second amendment

This addendum is not a new number: request numbers from #40 belong to PR #25.

- **What happened:** EXP-051 session 1 left no evidence. The trace analysis loaded the whole decoded trace into memory (18 GB), Ctrl+C could not reach it, and the owner ended the process. EXP-051 is ABANDONED.
- **The fix:** `tools/m1/netwatch.py` streams the trace and keeps only counts. Codex passes 14 and 15 found 4 defects; 3 are fixed and 1 is documented. The record is in `docs/m1-criteria.md` §8 and §7 item 4.
- **Please check:**
  1. Is the streamed analysis equivalent to the whole-file one for tracerpt output? The one documented difference is the exception named for a file that is both malformed and truncated.
  2. Is the limit acceptable: an interrupt inside the library's open call can leave the raw file open, which then ends as a FAIL (condition 10)?
  3. Should the tool write a partial `session.json` before the analysis, so that a killed analysis still leaves the snapshots and the operator's answers? This is not done: it would change `tier_a.py` and needs its own approval.
- **Next:** EXP-050 and the dry run must pass elevated on the amended code, and the owner must approve, before EXP-060 session 1.
  - **Update 2026-10-29:** both passed elevated (the owner's runs on clean `7126ff0`, code `f1665ae`): coverage 9/9 MATCH, with 0 events lost of 200254 under the flood and of 600696 under the mixed load; dry run 11/11. Evidence: `evidence/logs/m1-coverage-2026-10-29-063355-elevated`, `evidence/logs/m1-dryrun-2026-10-29-063519-elevated`. The owner's approval is still pending.
  - **Update 2026-10-29, later:** the owner approved it as of `a8f5d5a`. `tier_a.py` was then found to still label `session.json` "EXP-051"; the label now says "EXP-060" (one constant and the docstring), which the owner approved as of `c4e9cce`. EXP-060 is recorded, NOT_RUN, and its session 1 may run.
  - **Update 2026-10-30: EXP-060 session 1 FAIL** (`evidence/logs/m1-tier-a-2026-10-30-140325-s1/session.json`). Its only reason is the owner's "no" to "Did you close the game through its own menu?"; the game exited with code 0 in the trace, and the owner reports the answer as a mistake. Network clean and covered, 0 of 546803 events lost, no contact; the streamed analysis finished; no position observed in 8 files. EXP-060 is FAILED and the series is stopped. **A question for you:** the closing questions change polarity (yes wanted, then no, no) and take one key with no confirmation. Should the next series confirm each answer, or ask all three with the same polarity? That would be a session-tool change and a new approval.
  - **Update 2026-10-30, later:** the owner asked for the fix. Third amendment (`tier_a.py` only): yes/no typed in full; the closing answers read back and confirmed, `no` asking again; end of input before confirmation leaves all unanswered. Codex pass 16 found 1 defect, fixed. The next series is EXP-061 (NOT_RUN), after the owner's approval.

- **Date:** 2026-10-28
- **Requested by:** Claude Code
- **Branch:** `feature/d008-gog-inventory` (PR #26).
- **Previous requests:** #1–#37 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md). #38 belongs to PR #25 (KI-35).
- **Records:**
  - experiment: `evidence/experiments/EXP-048.json` (PARTIAL);
  - evidence: `evidence/logs/m1-tier-a-2026-10-28-113203-s1/session.json`;
  - criteria: `docs/m1-criteria.md` §8, "Session 1";
  - known issue: KI-45;
  - **amendment (added the same day, at the owner's request):** `docs/m1-criteria.md` §4a, §6a, §7 item 3 and the "amended" items in §8; `tools/m1/netwatch.py`, `coverage.py`, `tier_a.py`; tests `tools/m1/test_m1.py` class `KI45`; new records `EXP-050` and `EXP-051` (both NOT_RUN).
- **Please reply** as a comment on PR #26. Do not merge or push to `main`. EXP-048's tools and its §8 are as approved. **The owner approved the amendment on 2026-10-28** (commit `fe0af56`, code `322f02d`), before this review; it has not run. `tier_a.py` must not be run until EXP-050 and the dry run have passed elevated on that code. Your review still applies: a defect you find would mean a code change and a new approval.

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

## 2. The amendment (written at the owner's request; approved by the owner 2026-10-28; not run)

- **(a) Buffers.**
  - `netwatch.start` passes `-bs 256 -nb 64 512` (`netwatch.BUFFERS`, at most 128 MB). The providers and keywords are unchanged.
  - The trace state, the analysis and `session.json` keep the buffers and `events_total`.
  - **EXP-050** measures the buffers. `coverage.py` gains `flood_with_session_buffers`: the 200000-datagram loopback flood that lost events with the tiny buffers. It must be covered, with 0 events lost, the flood attributed, and the stand-in's record that every send completed.
  - The owner runs it elevated together with `dryrun.py`, which must still give 11/11.
- **(b) Encoding.**
  - A file with no BOM that is not valid UTF-8 is read as Windows-1252 and listed as "read as Windows-1252".
  - In such a read, a line with a non-ASCII character that contains `pos` or `loc` anywhere is a candidate, so it is an invalid observation unless it reads exactly. A UTF-8 line inside a Windows-1252 file cannot lose its label to the decoding.
  - A file read one way at one end and another way at the other is unreadable evidence. The ways are plain, Windows-1252 and known binary.
  - A NUL without a BOM, a byte undefined in Windows-1252, or a BOM that does not decode stays unreadable.
- **(c) `user/gpu.nfo`.** When it does not decode and contains a NUL, it is "known binary: hashed, not searched", not unreadable. As text, UTF-16 included, it is searched.
- **(d) The limit of NO DATA** is now stated exactly in §8: no new line matched the candidate rule. A label whose gap before the numbers is longer than 12 characters or holds a digit, sign or decimal point (`position -> 1 2 3`), a split or translated label, or binary data are not seen (Codex pass 12).
- **(e) Records.** EXP-048 stays PARTIAL. The amended series is **EXP-051**, numbered from session 1. Its expected result allows the two zero-count reasons that come with NO DATA.
- **Codex review (read-only):**

  | Pass | Defects | Examples |
  |---|---|---|
  | 9 | 3 | gpu.nfo skipping UTF-16 text; mixed-encoding lines losing their label; a MATCH from an incomplete flood |
  | 10 | 4 | it found the pass 9 non-ASCII-to-space projection unsafe (a `1½` read as 1, an encoding switch making old lines new, a widened gap); a soft hyphen in a label (documented as a limit) |
  | 11 | 3 | gpu.nfo binary-to-text; the NO DATA wording; EXP-051's expected reasons |

  All are fixed with regressions, or documented. Pass 12 found the amendment consistent, apart from one wording point about the gap, now fixed.
- **Tests:** 33 self-checks pass, 1 skipped. Each new test fails against the code it fixes.

## 3. Questions

1. Do you agree that session 1 cannot establish NO DATA, and that PARTIAL is the right result for EXP-048?
2. Is the amendment the minimum change? Is the Windows-1252 rule, with non-ASCII `pos`/`loc` lines as candidates, sound, or should known legacy files be listed by name instead?
3. Is EXP-050's single flood case enough to size the buffers, given that a real session's background load is not known? The session record now keeps `events_total`, so session 1 of EXP-051 also measures it.
4. Is a new series (EXP-051, from session 1) the right way to continue after a stopped series?

## 4. Open issues

- KI-45 (above).
- The second player's GOG copy.
- KI-35 (PR #25, separate).
