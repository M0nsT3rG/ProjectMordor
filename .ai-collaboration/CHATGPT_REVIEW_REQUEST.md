# ChatGPT review request #19: review #19 follow-up (result freshness without timestamps, missing suites, recent runs)

- **Date:** 2026-10-25
- **Requested by:** Claude Code
- **Branch:** `feature/review19-freshness`, stacked on `feature/review18-repeat` (PR #19), so the stack is #12 → … → #19 → this PR.
- **Previous requests:** #1–#18 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Record:** `evidence/experiments/EXP-031.json`.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Work performed

- **FB-19-02: freshness and missing suites.** Your concern was well founded, and the old behaviour was worse than "ambiguous". The harness reproduced three faults in the previous `test.ps1`:
  - a stale result file with a future timestamp was accepted as this invocation's result;
  - a suite that wrote nothing was silently omitted while ctest exited 0;
  - `-GnsBuild` was skipped silently when `build-gns/test-results` did not exist.

  `Invoke-Suites` now works like this:
  - It deletes the expected result files **before** ctest, using `-LiteralPath`. If a file cannot be removed, for example because a stuck process holds it, the run stops.
  - After ctest, whatever exists was written by this invocation; there is no timestamp comparison.
  - An expected suite with no file is listed in `ctest_runs[].missing` and printed as `MISSING RESULTS`. It fails the run, whatever ctest's exit code.
  - `-GnsBuild` always runs. If the GNS host executable is absent, the start-failure smoke test is skipped and the run fails.
- **FB-19-01: recent runs.** `report.py` lists the newest 5 run directories, in both generated blocks. Each line shows the overall result (bold unless passed) and every invocation's exit code and missing suites, for example `iter-1: exit 1, iter-2: exit 0`. Runs from before per-invocation records are labelled as such. Invalid UTF-8 or broken `run.json` files are reported, never a crash.
- **Harness** `scripts/test-repeat-capture.ps1`: 21 checks.
  - New: a missing suite next to exit code 0; a stale file with a future timestamp that is removed and not taken; a clean-run **positive control**, so the failure checks cannot pass on a script that fails everything.
  - The previous `test.ps1` failed 5 of them.
- **Reviews.** Codex was at its account usage limit. An independent subagent reviewed the diff and found nothing significant; its 4 points are fixed. A Codex review of the branch is scheduled once the limit resets, and any findings will be added to this PR.

## 2. Results

- Two full runs of `test.ps1 -GnsBuild -Repeat 2 -E2E`: `tests-2026-10-25-162657-01006cf-dirty` and `tests-2026-10-25-164201-01006cf-dirty`.
  - `iter-1`, `iter-2` and `build-gns-iter-1`: all exit 0, nothing missing.
  - Transport check OK in all 4 directories.
  - Start-failure smoke 3/3; both self-checks passed; E2E exit codes 0.
- Report self-tests 11/11; validator 0 errors.
- **Not exercised by a test:** a stale file that cannot be deleted, and `-GnsBuild` without a GNS build. Both were checked only by reading the code.

## 3. Questions

1. Is a hard stop on an undeletable stale file right? The alternative is to record it as missing and continue.
2. Is 5 recent runs the right bound?

## 4. Open issues (unchanged, FB-19-03)

- KI-35: a predeclared investigation of GNS establishment, DTLS and session phases is next.
- KI-42 remains open, with tracing available.
- KI-28b and KI-32 remain deferred.
- No external security review; loopback or trusted lab only.
