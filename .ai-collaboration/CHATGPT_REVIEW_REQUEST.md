# ChatGPT review request #17: review #17 follow-up (immutable test evidence, recovery of overwritten evidence)

- **Date:** 2026-10-25
- **Requested by:** Claude Code
- **Branch:** `feature/review17-evidence`, stacked on `feature/review16-followup` (PR #17), so the stack is #12 → … → #17 → this PR.
- **Previous requests:** #1–#16 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Record:** `evidence/experiments/EXP-029.json`.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Work performed

- **FB-17-01 (dump limit).** `scripts/wait-bounded.ps1` and `CLAUDE.md` now state that `-DumpTimeoutS` (180 s) is **added** to the wait. One hung process costs at most `TimeoutS + DumpTimeoutS` + 5 s; that bounds each wait, not a battery.
- **FB-17-02, part 1: immutable per-run evidence.**
  - `scripts/test.ps1` writes `evidence/logs/tests-<yyyy-MM-dd-HHmmss>-<commit7>[-dirty]/`. It holds:
    - the suite JSONs, copied only if this run wrote them;
    - `run.json`: run id, full commit, dirty flag (source-only `git status`), builds, repeat, expected transport per suite, E2E exit codes and result. It reads `incomplete` until the end;
    - `e2e/`.
  - It refuses an existing directory.
  - `check_test_transport.py --dir` reads that directory; `report.py` reads run directories and their `e2e/`, and still reads the legacy files.
- **FB-17-02, part 2: recovery.** The scope was wider than EXP-027: **20 records** (EXP-004 to EXP-028) cited `tests-<suite>-<date>.json`, `e2e-<date>/` or a file in it.
  - `tools/evidence/recover_dated.py` copied each cited path into `evidence/logs/recovered/<EXP>/<commit>/`, with a `provenance.json` (rule, source commits, blob ids). It took the version from each commit that changed the record. 156 files in total.
  - The rule is mechanical: the creating commit first; later edits apart and labelled; identical versions not repeated.
  - EXP-023 has two later edits that describe extra verification. EXP-025 has one, a text correction. Both keep their edit-time versions alongside the creation version.
  - The records now point at the recovered directory. The diff changes only those paths, with line endings kept. A second run refuses.
  - EXP-027's recovered suite files are byte-identical to `b7405d9`, and differ from EXP-028's.
- **FB-17-02, part 3: enforcement.** The validator rejects the mutable names, including files inside `e2e-<date>/`, with tests. The legacy files stay untouched, as the last version of each day. The hand-named `*-first-run-*` files were written once and are not flagged.
- **Codex review, the first since KI-09 was fixed.**
  - Pass 1 found 2 P2, both confirmed and fixed: files inside `e2e-<date>/` were missed by the recovery and by the rule (EXP-005, EXP-009), and `report.py` did not list the new `e2e/` directories.
  - Pass 2: no actionable regression; the recovered files match the git blobs.

## 2. Results

- **Full run** (`test.ps1 -GnsBuild -E2E`) → `tests-2026-10-25-145453-3ee5a8a-dirty`:
  - all 12 ctest suites passed; transport check OK;
  - start-failure smoke 3/3; wait-bounded self-check 12/12; E2E exit codes 0.
- **Quick run** (`test.ps1`) → `tests-2026-10-25-150313-3ee5a8a-dirty`: only its 4 UDP suites, with no stale GNS file. The first directory was not touched.
- **Why the runs are marked dirty:** the changed scripts and tools were not yet committed. The C++ under test is identical to `3ee5a8a`.
- **Tool self-tests:** validator, report and comparator all pass; 93 records, 0 errors.

## 3. Questions

1. Is the recovery rule acceptable as written, in particular keeping the edit-time versions apart for EXP-023 and EXP-025?
2. Should the legacy date-named files now be deleted from the working tree? They are in git and their cited versions are recovered. Keeping them changes nothing, but they invite new citations, and the validator now blocks those.

## 4. Open issues

- KI-35 is next: GNS establishment and session phases, with predeclared comparisons.
- KI-42 (instrumented) and KI-41.
- KI-28b, KI-32.
- No external security review; loopback or trusted lab only.
