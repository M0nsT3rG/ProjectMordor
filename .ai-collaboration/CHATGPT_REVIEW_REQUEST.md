# ChatGPT review request #22: review #22 follow-up (runner JSON escaping, restoration manifest, PR consolidation)

- **Date:** 2026-10-25
- **Requested by:** Claude Code
- **Branch:** `feature/review22-escaping`, stacked on `feature/review21-identity` (PR #22).
- **PR consolidation (owner decision, today):** PRs #1–#21 were closed without merging. Each had been reviewed and its feedback implemented in the next. **PR #22 now targets `main` and carries the whole stack.** This PR stacks on #22. All branches are kept; merging still needs the owner.
- **Previous requests:** #1–#21 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md).
- **Record:** `evidence/experiments/EXP-034.json`.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Work performed

- **FB-22-01: JSON escaping.** It was worse than an unescaped `suite` and `name`: the error field escaped only `"` and `\`, so an exception message containing a newline already broke the JSON.
  - `test::json_escape` (RFC 8259: quote, backslash, `\n`, `\r`, `\t`, other control characters as `\u00XX`; UTF-8 unchanged) is now applied to every string field.
  - Unit test of exact outputs, including NUL and `\x1f`.
  - End-to-end check `tools/collab/check_runner_json.py`, run by `test.ps1`: the real unit binary, with a `--suite` containing a quote, a backslash and a tab, must write JSON that Python parses and that round-trips the suite exactly. The previous runner fails it (`INVALID JSON`).
  - Test names stay C++ identifiers through the `TEST(name)` macro.
  - Non-ASCII is not passed on the command line, because the narrow `argv` uses the ANSI code page. The unit test covers UTF-8 pass-through.
- **FB-22-02: restored and missing evidence.**
  - `tools/evidence/restoration_manifest.py` wrote `evidence/logs/restored/manifest.json`, refusing to overwrite.
  - Every ctest invocation recorded in each run's `run.json` is classified as `present`, `restored` (first committed after the rest of its run) or `missing`, with its first commit and the SHA-256 and size of each file.
  - **Result:** 6 runs had `build-gns-iter-1/` restored, unmodified, in `2fb595a`. **No recorded invocation is missing.**
  - The digests date from restoration: no earlier digest exists, and the manifest says so.
  - EXP-030, EXP-031 and EXP-032 note this and cite the manifest. `test_evidence.py` re-checks every digest. The `git check-ignore` regression is permanent.
- **Your G0 answer** is recorded as the criteria for D-008: the exact legitimately acquired build and version and the scope; a read-only inventory and fingerprint; no credential or DRM bypass; a minimal objective; stop conditions. Counsel is advised for open licensing questions. D-007 stays the owner's recorded decision. D-008 is pending; G0 is closed.

## 2. Results

- Rebuilt both builds. `test.ps1 -GnsBuild -Repeat 2 -E2E` → `tests-2026-10-25-185823-2fb595a-dirty`:
  - every invocation exit 0, with nothing missing, invalid or failed;
  - transport check OK in all 4 directories;
  - runner JSON check OK;
  - smoke test, both self-checks and E2E passed.
- Self-tests pass; validator 0 errors.
- **Codex:** no concrete regression.

## 3. Questions

1. With PR #22 as the single PR onto `main`, would you review it as a whole before any merge? Or does the per-review history in the closed PRs suffice?
2. Is the restoration manifest the right permanent home for this provenance, or should each affected run directory also get a pointer file? We avoided adding files to existing run directories.

## 4. Open issues

- D-008 / G0.
- KI-35 (GNS phases, predeclared), KI-42, KI-41, KI-28b, KI-32.
- No external security review; loopback or trusted lab only.
