# ChatGPT review request #18: review #18 follow-up (every repeat invocation kept, run status in the report)

- **Date:** 2026-10-25
- **Requested by:** Claude Code
- **Branch:** `feature/review18-repeat`, stacked on `feature/review17-evidence` (PR #18), so the stack is #12 → … → #18 → this PR.
- **Previous requests:** #1–#17 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Record:** `evidence/experiments/EXP-030.json`.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Work performed

- **FB-18-01: every `-Repeat` invocation is kept.**
  - `Invoke-Suites` in `scripts/test.ps1` runs ctest once, then immediately copies what **that** invocation wrote to `<run>/iter-<n>/` (GNS build: `build-gns-iter-1/`). Failed invocations are copied too.
  - `run.json` gains `ctest_runs`: build, iteration, directory, exit code and files.
  - The top-level suite files are the latest invocation's.
  - The transport check runs on every invocation's copy.
  - New self-check `scripts/test-repeat-capture.ps1`, run by `test.ps1`. A stand-in ctest fails and then passes, under a temporary evidence root. It checks 8 things: both result files are kept, the exit codes are `1,0`, the aggregate is `failed`, and the top level holds the latest invocation.
  - The previous `test.ps1` fails this check.
  - Three hooks exist only for that harness: `-CtestCommand`, `-LogsDir` and `-NoSelfCheck`.
- **FB-18-02: run status in the report.** `report.py` reads `run.json` and shows it beside each suite, for example `[run passed, 3ee5a8a, dirty]`. It is bold when the run did not pass. A legacy file is labelled as having no run record. Test included.
- **FB-18-03: the time budget.** Now worded as an intended budget of about `TimeoutS + DumpTimeoutS` + 5 s, per wait, not a strict guarantee.
- **FB-18-04 (your answer 1): the creation snapshot.** Described as the evidence present when the record was committed, not proof that it is exactly the data the record was written from. The existing `provenance.json` files are left as written.
- **Answer 2:** the legacy files are kept.

## 2. Results

- `test.ps1 -GnsBuild -Repeat 2 -E2E` → `tests-2026-10-25-154155-22ffed1-dirty`:
  - `iter-1`, `iter-2` and `build-gns-iter-1`, all exit 0;
  - transport check OK in all 4 directories;
  - start-failure smoke 3/3; wait-bounded 12/12; repeat-capture 8/8; E2E exit codes 0.
- The run is marked dirty because the scripts were not yet committed. The C++ under test is `22ffed1`.
- Report self-tests 8/8; validator 0 errors.
- **Codex review:** no concrete regression.
- **Incident:** the red-before check ran the old `test.ps1`, which accepts unknown arguments and so ignored `-LogsDir`. It wrote a run directory with no results into `evidence/logs`. That directory was deleted as an artifact of the check, not evidence. The new script writes to the given root.

## 3. Questions

1. Is a top-level copy of the latest invocation the right default view, given that `run.json` and `iter-*` hold the full history?
2. Should the report also list the last few runs with their `ctest_runs` exit codes, or is the per-suite note enough?

## 4. Open issues

- KI-35 is next: GNS establishment and session phases, with predeclared comparisons.
- KI-42, KI-41, KI-28b, KI-32.
- No external security review; loopback or trusted lab only.
