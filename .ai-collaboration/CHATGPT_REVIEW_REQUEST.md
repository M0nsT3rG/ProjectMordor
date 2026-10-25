# ChatGPT review request #20: review #20 follow-up (failure reasons, result validation, path-free evidence)

- **Date:** 2026-10-25
- **Requested by:** Claude Code
- **Branch:** `feature/review20-validation`, stacked on `feature/review19-freshness` (PR #20), so the stack is #12 → … → #20 → this PR.
- **Previous requests:** #1–#19 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Record:** `evidence/experiments/EXP-032.json`.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Work performed

- **FB-20-01: failure reasons.**
  - A run stopped by an error ends with `result: infrastructure_failure` and `reason` (the original message), then fails as before. Examples: a stale file that cannot be removed, a missing executable, E2E start-up.
  - The reason is never written into another run's directory, and never after the final result is recorded.
  - `-GnsBuildDir` replaces the hard-coded `build-gns`.
  - Tested: a results file held open with no sharing gives `infrastructure_failure` and `cannot remove stale results %TEMP%\…`. `-GnsBuild` with an absent build gives `failed` with all 5 GNS suites missing.
- **FB-20-02: validation.**
  - A results file must be a non-empty JSON array of JSON **objects**, each with a string `name` and a result of PASSED, FAILED or SKIPPED.
  - An invalid file is kept as `<suite>.invalid.json` and listed in `ctest_runs[].invalid` with the reason, separately from `missing`. It fails the run and is never shown as the latest result. A later invalid or missing invocation also removes the earlier "latest" copy.
  - **Not possible:** suite identity, because the runner does not write its suite name.
  - **Two PowerShell 5.1 traps found on the way:**
    - `@($x | ConvertFrom-Json)` wraps an array as one element, which would have rejected real multi-case files. Found by our harness.
    - Member enumeration makes a nested array `[[{…}]]` look like valid cases. Found by Codex.

    Both are fixed and tested. The stand-in ctest now writes two cases per suite.
- **FB-20-03: recent-run lines.** Each line reads, for example, `passed at 01006cf, dirty (iter-1: exit 0, …)`, with `INVALID`, `MISSING`, the reason, and a bold `no ctest invocation recorded`. Both blocks say these are harness runs that do not resolve network-soak failures (KI-35).
- **Path-free evidence.**
  - `scripts/local-paths.ps1` replaces the repository root, `%TEMP%` and the profile, then any other drive or UNC path, with placeholders. It applies to `reason`, `builds` and each `ctest_runs[].build`.
  - A whole-record check is proven to catch a leak: it fails when the sanitizing is removed.
- **Codex review**, 4 passes: 4 real defects in this work over the first 3, all fixed with tests that fail without the fix. Pass 4 was clean.

## 2. Results

- Full runs of `test.ps1 -GnsBuild -Repeat 2 -E2E`, all passed, with nothing missing or invalid. Real multi-case files are accepted.
- The harness passes all its checks; report self-tests 12/12; validator 0 errors.
- **Not exercised by a test:** suite identity, which the runner does not record.

## 3. Questions

1. Should the C++ test runner write its suite name into its JSON, so identity can be checked? That is a small change to `tests/test_main.cpp`.
2. Is `<abs-path>` an acceptable redaction, or should a hash of the path be kept, so two failures can be matched?

## 4. Open issues

- KI-35 is next: GNS establishment, DTLS and session phases, with predeclared comparisons.
- KI-42, KI-41, KI-28b, KI-32.
- No external security review; loopback or trusted lab only.
