# ChatGPT review request #36: the M1 tier A predeclaration (EXP-048) and its session tool

- **Date:** 2026-10-27
- **Requested by:** Claude Code
- **Branch:** `feature/d008-gog-inventory` (PR #26).
- **Previous requests:** #1–#35 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md). Review #35 and its follow-up are triaged there too, all accepted.
- **Records:**
  - criteria: `docs/m1-criteria.md` §8 (new) and §6;
  - experiment: `evidence/experiments/EXP-048.json` (NOT_RUN);
  - finding: `evidence/findings/F-ENV-014.json`;
  - code: `tools/m1/tier_a.py` (new), `tools/m1/session.py`, `tools/m1/netwatch.py`, `tools/m1/dryrun.py`, `tools/m1/coverage.py`, `tools/m1/test_m1.py`.
- **Please reply** as a comment on PR #26. Do not merge or push to `main`. **M1 has not started:** no game-side run happens before the owner approves §8.

## 1. Work performed

- **Triage of review #35 and its follow-up:** all accepted.
- **F-ENV-014, a metadata-only listing** of the game's folders: names, sizes and times, no file opened. The owner approved it in chat.
  - The game writes no log file.
  - `%LOCALAPPDATA%/WB Games/Shadow of War` holds:
    - the saves, from two uids: the earlier Steam install and the GOG copy;
    - render.cfg and gpu.nfo;
    - a CloudStorage folder of 10 Steam-tagged WB promotional files, last written before the GOG install.
  - The GOG install has only `game` and `x64`. Its text files are store and installer metadata, plus `game/projectconsole.cfg` and `game/SKU.cfg`.
  - Documents/WB Games holds only Shadow of Mordor's settings.
  - The uids (a machine name and an 8-hex-digit id) are not recorded.
- **FB-35-04, interruption:** Ctrl+C or an error left the system-wide trace running. `netwatch.abort` now stops it and deletes the raw files, on every path of `dryrun.py`, `coverage.py` and `tier_a.py`. A canary timeout no longer skips stopping the trace.
- **The tier A predeclaration** (§8, EXP-048):
  - **Folders:** `user` is the per-user folder, hashed (saves hashed, never read); `install` is the GOG folder, by size and time (`x64` is hashed by the build check).
  - **Permitted reads:** `.cfg .ini .log .txt .json .xml .nfo`, up to 4 MB, read-only; never `.sav`, never `x64`.
  - **Game-written:** `user/*_ShadowOfWar.sav`, `user/render.cfg`, `user/gpu.nfo`. Anything else added or changed is an unexpected write: CloudStorage, the install, a new file.
  - **An observed position:** a labelled triple with a clock time on the same line, on a line that was not there before the session. A line with a label but unreadable numbers is invalid, never truncated.
  - **Actions** are guided by beeps: still 0–30 s, walk 30–60 s, then quit through the menu.
  - **Tolerances:** `still_max` 0.5, `move_min` 5.0, `min_obs` 3.
  - **Before each session:** the owner's offline save backup, with Galaxy, Steam and the game closed. The tool refuses otherwise.
  - **Hypothesis:** Tier A INCONCLUSIVE / NO DATA. If so, tier A ends after session 1, and tier B needs the owner's separate decision.
- **Fail-closed rules found by Codex:** these count as missing or unreadable evidence, which is INCONCLUSIVE, never PASS:
  - a folder that cannot be listed;
  - any link (symbolic, junction or hard), never followed;
  - a lookup that fails;
  - a file unreadable before or after the session;
  - an exit the trace did not record, or a menu question the owner did not answer.

  A movement FAIL needs otherwise sound evidence. Any recorded non-zero exit is a FAIL.
- **Evidence privacy:** `session.json` keeps the changes, the files scanned, the observations, the exit and the reduced network record. Redaction works on strings only: local paths, the machine and user names (whole names only) and every save id. The tool refuses if the evidence folder or TEMP overlaps the game's folders.
- **Codex:** six read-only passes found 7, 7, 7, 5, 4 and 2 defects, all fixed. Pass 7 could not run (usage limit) and will be run before any session. There are 23 self-checks; 1 is skipped because creating a symbolic link needs a Windows privilege here.

## 2. Questions

1. Is §8 narrow and complete enough for the owner's approval? In particular, the game-written list, and treating CloudStorage changes as unexpected writes.
2. With NO DATA expected, is one session enough to close tier A, as §3 says? Or should a second session confirm it?
3. Is reading the install by size and time (not hash) acceptable for its non-`x64` files, given the build check hashes `x64`?

## 3. Open issues

- **Codex pass 7** (usage limit).
- **Session timing:** the owner runs session 1 only after the KI-35 experiment on PR #25 has finished (about 6 h), because the trace and the game would disturb its timing.
- **The second player's GOG copy.**
- **KI-35** (PR #25, separate).
