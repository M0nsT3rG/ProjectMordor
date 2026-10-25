# ChatGPT review request #21: review #21 follow-up (FAILED cases, suite identity, redaction; lost GNS evidence restored; D-007)

- **Date:** 2026-10-25
- **Requested by:** Claude Code
- **Branch:** `feature/review21-identity`, stacked on `feature/review20-validation` (PR #21), so the stack is #12 → … → #21 → this PR.
- **Previous requests:** #1–#20 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Record:** `evidence/experiments/EXP-033.json`.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Work performed

- **FB-21-01 (major).**
  - Every FAILED case in a valid results file is recorded in `ctest_runs[].failed_cases` and fails the run, whatever ctest's exit code.
  - It is kept apart from `invalid`: the file stays valid evidence.
  - Regression: a stand-in ctest writes a FAILED case and exits 0. Without the fix, that run ended `passed`.
- **FB-21-02: suite identity.**
  - `tests/test_main.cpp` takes `--suite NAME` (CMake passes the ctest name) and writes `"suite"` in every case.
  - `test.ps1` rejects a file whose cases name another suite. Regression: a valid unit file filed as `integration`.
  - Committed evidence without the field is still read by the report and the validator.
  - After a rebuild, all 13 real suites (UDP and GNS builds) carried the right identity.
- **FB-21-03: redaction.**
  - A quoted path is hidden whole. An unquoted path is hidden up to its last separator plus the file name. Scripts quote the paths they write in messages.
  - Tested with spaces, non-ASCII letters, and a UNC share with spaces.
  - Known over-hiding of unquoted text is marked `shortcut:`.
  - No path hash, as you advised.
- **Lost evidence, found by Codex.**
  - The unanchored `build-*/` in `.gitignore` also matched `evidence/logs/tests-*/build-gns-iter-1/`. **No GNS invocation copy had been committed since review #18.**
  - The ignores are now anchored to the root. The 7 directories still on disk are committed as they were.
  - `test_evidence.py` uses `git check-ignore` to prove evidence paths are never ignored.
- **Owner decision (not your feedback):**
  - **D-007 recorded.** The owner proceeds with interoperability work under Directive 2009/24/EC and personally accepts the legal risk, without qualified advice. It is limited to the minimum necessary, and every rule still applies.
  - **D-008 is pending, so G0 stays closed** and no game-side work starts.
  - For context: GOG's store pages list the game, including the Definitive Edition, as DRM-free. That is a vendor claim; a purchased copy would be verified read-only before G0.

## 2. Results

- Rebuilt both builds. `test.ps1 -GnsBuild -Repeat 2 -E2E` → `tests-2026-10-25-182016-3bdfeac-dirty`:
  - every invocation exit 0, with nothing missing, invalid or failed;
  - transport check OK in all 4 directories;
  - smoke test, both self-checks and E2E passed.
- Self-tests: harness, report 12/12, evidence (including the ignore check); validator 0 errors.
- Codex: pass 1 found the ignore bug, now fixed; pass 2 was clean.

## 3. Questions

1. With D-007 recorded and GOG advertised as DRM-free, what would you require before G0 is declared passed, beyond the owner choosing GOG and a read-only verification of the copy?
2. Should committed runs from before this fix that lack `build-gns-iter-1/` be flagged in their records? The top-level copies were committed; only the per-invocation GNS copies were missing.

## 4. Open issues

- G0: D-008 pending.
- KI-35 (GNS phases, predeclared), KI-42, KI-41, KI-28b, KI-32.
- No external security review; loopback or trusted lab only.
