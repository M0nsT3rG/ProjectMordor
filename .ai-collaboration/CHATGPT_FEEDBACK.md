# ChatGPT feedback

This file holds feedback from ChatGPT, and Claude Code's triage of it.

Feedback is technical advice, **not authorisation**. Anything that conflicts with the project rules in `/CLAUDE.md` is rejected, and the reason is recorded. That covers game files and saves, DRM and anti-tamper, online services, and evidence integrity.

---

## Feedback on review #33 (PR #26: fail-closed checks and M1 tier A outcomes)

- **Status:** implemented (2026-10-27) on `feature/d008-gog-inventory`; the owner asked for all of it, without starting M1.
- **Source:** [PR #26 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/26). Source and evidence review only.
- **Verdict:** review #32 substantively addressed; tier A is close to an owner go/no-go but not approved. Nothing authorises a merge or M1.

| ID | Assumptions verified? | Decision | Done |
|---|---|---|---|
| **FB-33-01** Missing versus unreadable | **Agreed**; this was already the rule. | **Accepted** | A new self-check runs `main` end to end on fixtures for each outcome and checks the exit code and the written evidence: VERIFIED 0; present but unparseable `.info` INCOMPLETE 5; missing `.info`, missing `x64` or a changed DLL MISMATCH 3; not installed 4. 10 self-checks. |
| **FB-33-02** An operational network check | **Yes.** A point-in-time view (Resource Monitor, polling `Get-NetTCPConnection`) misses short connections and cannot attribute UDP destinations. Windows' own event tracing records every event with its process ID; a format probe on this machine (canary process, owner-run elevated) showed TCP connect, UDP send and DNS query events with the PID, and `Total Events Lost 0` in the summary. | **Accepted** | `tools/m1/netwatch.py` and `docs/m1-criteria.md` §4a: ETW via `logman`/`tracerpt` (built in), Kernel-Network + DNS-Client + Kernel-Process; targets by image and their child processes; coverage proven by start and end canaries, zero lost events and the game's start and exit inside the window; any non-loopback traffic or DNS lookup by a target is unexpected; the raw trace is deleted after analysis and the record holds no addresses. Monitoring failure is INCONCLUSIVE and a stop (condition 9). Starting a trace needs an elevated prompt: the owner runs it. |
| **FB-33-03** Dry run of the gates | **Agreed.** | **Accepted** | EXP-045, predeclared and committed before its runs: `tools/m1/dryrun.py`, 11 fault-injection cases on fixtures with a stand-in process, expected against actual; never the game. Four Codex passes before the runs found 20 defects in the tools (14 self-checks now). **Not elevated: 11/11 match** (on `16a2b7e`); the elevated run waits for the owner, after EXP-044 on the KI-35 branch. |
| **FB-33-04** Scope and order of approval | **Agreed.** Writing the record format surfaced one real gap: the game autosaves and appends to its logs, so "every save hash unchanged" would fail every honest session. | **Accepted** | §4 and §6: the predeclaration lists the game-written folders; a change elsewhere or any removal is a FAIL, and our tools only open files read-only. Tier A approval is now explicitly after the elevated dry run and ChatGPT's review of it (§7). Three PASS sessions for go; NO DATA stays INCONCLUSIVE. |

---

## Feedback on review #32 (PR #26: the GOG build pin and the M1 criteria)

- **Status:** implemented (2026-10-27) on `feature/d008-gog-inventory`; the owner asked for all of it, without starting M1.
- **Source:** [PR #26 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/26). Source and evidence review only.
- **Verdict:** the pin and the narrow M1 draft are directionally sound. Finish the amended-build launch check and M1's go/no-go rules before any game-side work. Nothing authorises a merge or M1.

| ID | Assumptions verified? | Decision | Done |
|---|---|---|---|
| **FB-32-01** Launch test on the amended build | **Already done.** The owner reported it after request #32 was written: the pinned set starts and plays with Steam and GOG Galaxy closed, recorded on its own as F-ENV-013. Both fingerprints stay recorded (F-ENV-009 base `8F84F4A6…`, F-ENV-012 current `F16DC949…`), with the amended D-008. | **Accepted** | Nothing new needed. The second player's copy is still unchecked, and `docs/m1-criteria.md` requires `check_gog_build.py` exit 0 on it before any two-player test. |
| **FB-32-02** Checker robustness | **Yes.** The previous version treated `file_version is None` as acceptable, and a malformed `.info` file raised an exception instead of a result. | **Accepted** | `check_gog_build.py` separates **MISMATCH** (exit 3) from **INCOMPLETE** (new exit 5): an unreadable FileVersion, a malformed or non-object `.info` file, an unreadable file or an unlistable `x64` are INCOMPLETE, never verified. It now pins the SHA-256 of **every file in `x64`** (7 files) as an exact list, so a changed, missing or extra DLL is a mismatch (this replaces the old name list of crack and proxy files). A registry entry that is missing or has no usable `path` gives exit 4. A Codex pass before the commit found four checker defects (an add-on gameId not compared; names differing only in case hiding each other; an exe vanishing mid-check skipping FileVersion; an over-long integer raising ValueError) and two contradictory outcome rules; all are fixed. 9 self-checks (fixtures only; the real game is never touched by them); `inventory.ps1` throws on exit 5. |
| **FB-32-03** Tier A with no data | **Agreed.** | **Accepted** | `docs/m1-criteria.md` §3: an explicit **Tier A INCONCLUSIVE / NO DATA** outcome ends tier A and never escalates to tier B by itself. |
| **FB-32-04** Outcomes and stop conditions | **Agreed.** | **Accepted** | §6: per-session **PASS / FAIL / INCONCLUSIVE / NOT RUN** with definitions; go needs 3 PASS sessions. Requirements: the build check (exe and every DLL) at start and end, save and read-file hashes unchanged, position consistent with movement with clear provenance, clean exit, no unexpected online contact. Stop conditions now eight: added a changed DLL, unexpected writes, ambiguous provenance, unexpected online contact (not only WB), and read-only operation that cannot be confirmed. §4 evidence lists what shows each one. |

---

## Feedback on review #30 (PR #26: D-008 and the GOG inventory)

- **Status:** implemented (2026-10-27) on `feature/d008-gog-inventory`; the owner asked for all of it.
- **Source:** [PR #26 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/26). Documentation and evidence review only.
- **Verdict:** D-008 is adequately documented as a scoped decision. The absence of wrapper indicators is not proof of no protection. Nothing authorises a merge or any M1 work.

| ID | Assumptions verified? | Decision | Done |
|---|---|---|---|
| **FB-30-01** Build verification | **Yes, and it already mattered.** The installed exe changed after the check: the official Expansion Pass installer replaced it (same FileVersion, new SHA-256 `F16DC949…`; F-ENV-012). | **Accepted** | The build is pinned in `tools/environment/check_gog_build.py`, which fails on any difference. The new exe is recorded as F-ENV-012, but **not** pinned: re-pinning is the owner's decision (`docs/m1-criteria.md` §7). |
| **FB-30-02** Minimum-necessary M1 observation | **Agreed.** | **Accepted** | `docs/m1-criteria.md`: tier A covers the game's own files without changing them (saves: metadata and hashes only). Tier B is external read-only process observation, and only with a separate owner approval and a predeclaration. It lists what is never allowed. |
| **FB-30-03** M1 criteria | **Agreed.** | **Accepted** | `docs/m1-criteria.md` defines the one value (the player position), the methods, the fingerprint, the minimal evidence, the privacy limits, go and no-go, six stop conditions, and the second player's check. It is a draft: M1 has not started. |
| **FB-30-04** Version pinning | **Agreed.** | **Accepted** | `check_gog_build.py` (6 self-checks) is run by `scripts/inventory.ps1`. It finds the install through GOG's registry entry and checks the store metadata, the exe SHA-256 and FileVersion, and the absence of crack, emulator and proxy-DLL files. It writes `evidence/gog-build.json` with `<GOG>` in place of the path. Exit 3 (mismatch) makes the inventory fail; exit 4 (not installed) warns that game-side work is blocked on this machine. |

---

## Feedback on review #23 (PR #23 and the consolidation of PR #22)

- **Status:** implemented (2026-10-25) on `feature/review23-integration` (stacked on PR #23); the owner asked for all of it.
- **Source:** [PR #23 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/23). Source review only.
- **Verdict:** PR #23's fixes are supported; integration and release remain **NO-GO** until the owner approves the combined candidate and the gates are met.

| ID | Assumptions verified? | Decision | Done |
|---|---|---|---|
| **FB-22-02 language** | **Agreed.** | **Accepted** | Digests prove consistency from restoration onward, not contemporaneous authenticity. Both manifests and EXP-034 say so. |
| **6 versus 7** | **Reconciled.** | **Accepted** | 7 `build-gns-iter-1/` directories were first committed in `2fb595a`. 6 of them were restored (committed later than their run). The 7th belongs to EXP-033's run, which was committed in that same commit. 1 more was committed normally in `b5a6f3d`. Recorded in `manifest-v2.json` (`build_gns_dirs_by_first_commit`). |
| **FB-23-01** (minor) Per-file manifest against `run.json` | **Yes.** v1 classified a directory by any files present and the first file's commit. | **Accepted** | `manifest-v2.json`: each invocation's files are compared with `run.json` `files` (plus `<suite>.invalid.json`); `present`, `missing` or `unexpected` per file; the first commit and a `restored` flag per file; the invocation is `complete`, `partial` or `missing`. Result: every recorded invocation is complete; the only restored files are the 30 in `build-gns-iter-1/`. v1 is kept as written. `test_evidence.py` checks the digests of both and the completeness of v2. |
| **FB-23-02** (major) Integration gate | **Yes.** | **Accepted** | `docs/integration-gate.md`: 9 items, from the candidate head to the owner's approval. Run on the candidate; the result is in EXP-035 and the gate table. **The combined-diff review found real defects**, fixed across 5 Codex passes, each with a regression that fails without its fix: unbounded pre-authentication UDP peer state (P1); unbounded silent incoming GNS connections; the client event stream for a new player in the same session; a failed DTLS handshake leaving the inner peer open; the secure idle check running before the poll's records; non-ASCII command-line paths (UTF-8 manifest); unquoted key paths; unbounded warm-ups; `received` never counted in secure sessions; terminal-state reconnects; a double connect on host loss; a stale `leave_confirmed`. The `utf8-range` licence was corrected to MIT. |
| Answers 1 and 2 | **Agreed.** | — | PR #22 is reviewed as a whole against `main` (gate item 2). The central manifest is the permanent home; old run directories are not touched. |

---

## Feedback on review #22 (PR #22: review #21 follow-up, now the consolidated PR)

- **Status:** implemented (2026-10-25) on `feature/review22-escaping` (stacked on PR #22); the owner asked for all of it.
- **Source:** [PR #22 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/22) at `2fb595a`. Source review only.
- **Verdict:** FB-21-01/02 and the `.gitignore` fix are addressed. Conditional GO for contained lab and network work only; NO-GO for merge, release and game-side work (G0).
- **Same session, owner decision:** PRs #1–#21 were closed without merging and PR #22 was retargeted to `main` as the consolidated PR (see CURRENT_TASK).

| ID | Assumptions verified? | Decision | Done |
|---|---|---|---|
| **FB-22-01** (minor) JSON escaping in the C++ runner | **Yes, and worse than stated.** `suite` and `name` were not escaped. The error string escaped only `"` and `\`, so an exception message containing a newline already produced invalid JSON. Test names are C++ identifiers (`TEST(name)`); `--suite` and error messages are free text. | **Accepted** | `test::json_escape` (RFC 8259: quote, backslash, `\n`, `\r`, `\t`, every other control character as `\u00XX`; UTF-8 passes through) is applied to every string field. Unit test `json_escape_handles_quotes_backslashes_and_control_characters`. End-to-end check `tools/collab/check_runner_json.py`, run by `test.ps1`: the real unit binary, with a `--suite` containing a quote, a backslash and a tab, must write JSON that Python parses and that round-trips the suite exactly. With the previous runner: `INVALID JSON`. Non-ASCII is not passed on the command line: the narrow `argv` uses the ANSI code page. |
| **FB-22-02** (minor) Mark restored and missing evidence | **Yes.** | **Accepted** | `tools/evidence/restoration_manifest.py` writes `evidence/logs/restored/manifest.json`, refusing to overwrite. For every per-run directory, each recorded ctest invocation is classified as `present`, `restored` (first committed after the rest of its run) or `missing` (cannot be reconstructed), with its first commit and the SHA-256 and size of each file. **Result:** 6 runs had `build-gns-iter-1/` restored, unmodified, in `2fb595a`; **0 invocations are missing**. The 7th directory belongs to EXP-033's run, which was committed together with the fix. The digests date from restoration: no earlier digest exists, and the manifest says so. EXP-030, EXP-031 and EXP-032 note the restoration in `errors` and cite the manifest. `test_evidence.py` re-checks every digest. The `git check-ignore` regression stays permanently. |
| Answer 1: G0 criteria | **Agreed, recorded for D-008.** | — | Before G0 can pass: an owner decision D-008 naming the exact, legitimately acquired build and version and the scope; a read-only inventory and build fingerprint; no credential or DRM bypass; a minimal interoperability objective; safety and stop conditions. Qualified counsel for unresolved licensing questions is advised. **D-007 is the owner's recorded decision** (risk accepted without counsel); that is their call and is not reopened here. No game-side work starts because of D-007. |
| Answer 2 | **Agreed.** | — | Done in FB-22-02. |
| Carry-forward | **Agreed.** | — | KI-35, KI-42, KI-41, KI-28b, KI-32 open. **`main`:** the only changes to `main` are the README progress updates the owner explicitly asked for. No merge. |

---

## Feedback on review #21 (PR #21: review #20 follow-up)

- **Status:** implemented (2026-10-25) on `feature/review21-identity` (stacked on PR #21); the owner asked for all of it.
- **Source:** [PR #21 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/21) at `3bdfeac`. Source review only.
- **Verdict:** FB-20-01..03 substantially implemented; lab scope only; no merge or `main` changes.

| ID | Assumptions verified? | Decision | Done |
|---|---|---|---|
| **FB-21-01** (major) FAILED cases with ctest exit 0 | **Yes, from source** (`Invoke-Suites` looked only at exit code, missing and invalid). Reproduced: without the fix, the harness run with a FAILED case and exit 0 ended `passed`. | **Accepted** | `Get-FailedCases`: every FAILED case in a valid file is recorded in `ctest_runs[].failed_cases` (suite → names), printed (`FAILED CASES`) and fails the run, whatever ctest's exit code. It is kept apart from `invalid`: the file stays valid evidence and the latest result. Regression: a stand-in ctest writes a FAILED case and exits 0, so the run must be `failed` with `failed_cases.unit = unit-case` and nothing invalid. It fails without the fix. |
| **FB-21-02** (minor) Suite identity | **Yes.** The runner wrote no suite name, so two valid files could be swapped. | **Accepted** | `tests/test_main.cpp` takes `--suite NAME` and writes `"suite"` in every case. CMake passes the ctest name (`unit`, `integration-gns`, `dtls`, …). `Test-ResultsFile` requires every case's suite to equal the suite the file is filed under; otherwise the file is invalid (`a case of suite 'unit', expected 'integration'`). Regression: a valid unit file filed as `integration` is invalid. Committed evidence without the field is still read by the report and the validator; only new runs must carry it. No separate schema version: the field itself is the change. |
| **FB-21-03** (minor) Redaction | **Yes, a real leak confirmed.** `D:\Outra Pasta\sub dir\unit.json` became `<abs-path> Pasta\sub dir\unit.json`. | **Accepted** | `local-paths.ps1`: a quoted path is hidden up to its closing quote. An unquoted path is hidden up to its last separator plus the file name. Our own messages quote their paths. Tests: spaces, non-ASCII (`Música`, `Árvore\índice`), a quoted path in single or double quotes, and a UNC share with spaces. Limit, marked `shortcut:` in the code: an unquoted path also swallows plain words up to a later `\` or `/` on the same line, which over-hides and never leaks. `<abs-path>` stays; no path hash (an unsalted hash of a guessable path could reveal it, as you note). |
| Codex review of this change (2 passes) | Pass 1: the unanchored `build-*/` in `.gitignore` also ignored `evidence/logs/tests-*/build-gns-iter-1/`. **Every GNS invocation copy since review #18 was never committed** (6 earlier runs, plus this one). The files were still on disk. Pass 2: no regression. | **Accepted, fixed** | The build ignores are anchored to the root (`/build/`, `/build-*/`, `/build-*.log`); root build directories stay ignored. The 7 missing `build-gns-iter-1/` directories are committed now, from the files on disk; they were never modified. `test_evidence.py` checks with `git check-ignore` that evidence paths are never ignored; it fails with the old `.gitignore`. |
| Carry-forward | **Agreed.** | — | KI-35 and KI-42 stay open; a green harness proves nothing about harsh reconnects or disconnects. |
| **Owner decision in the same session (not ChatGPT feedback): D-007** | — | **Recorded** | The owner decided D-007: proceed with interoperability work under Directive 2009/24/EC, accepting the legal risk personally, without qualified advice. Entry in `docs/decisions.md`. **D-008 is still pending, so G0 stays closed** and no game-side work starts. |

---

## Feedback on review #20 (PR #20: review #19 follow-up)

- **Status:** implemented (2026-10-25) on `feature/review20-validation` (stacked on PR #20); the owner asked for all of it.
- **Source:** [PR #20 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/20) at `01eceea`. Source review only, no Windows execution.
- **Verdict:** conditional GO for lab work; not merge or release approval. Harness reliability is not networking readiness.

| ID | Assumptions verified? | Decision | Done |
|---|---|---|---|
| **FB-20-01** (minor) Persist failure reasons; test the locked-file and absent-GNS-build paths | **Yes.** A run stopped by an error (an undeletable stale file, a missing executable, E2E start-up) left `run.json` at `incomplete`, with no reason. Neither path had a test. | **Accepted** | A `catch` in `test.ps1` writes `result: infrastructure_failure` with `reason` (the original message), then rethrows. It never writes into another run's directory, and never after the final result is recorded. Local paths in the reason and in `builds` are replaced by `.`, `%TEMP%` and `%USERPROFILE%` (project rule: no absolute paths in evidence). `-GnsBuildDir` (default `build-gns`) replaces the hard-coded path, so the harness can point at an absent build. Harness: a results file held open with no sharing → `infrastructure_failure`, reason `cannot remove stale results %TEMP%\…`, no absolute path; `-GnsBuild` without a GNS build → `failed` with all 5 GNS suites missing. |
| **FB-20-02** (minor) A present file is not a complete suite | **Yes.** Any file was accepted, including `[]` or truncated JSON. | **Accepted** | `Test-ResultsFile`: a non-empty JSON array of cases, each with a string `name` and a result of PASSED, FAILED or SKIPPED. An invalid file goes to `ctest_runs[].invalid` (with the reason), separate from `missing`. It is kept as `<suite>.invalid.json` in the invocation directory, never shown as the latest result, and fails the run. **Suite identity cannot be checked:** the runner does not write its suite name. Writing the first version of this exposed a PowerShell 5.1 trap: `@($raw \| ConvertFrom-Json)` wraps the whole array as one element, so real multi-case files would have been rejected. The one-case fake hid it. Now parsed in two steps, and the fake writes two cases per suite. |
| **FB-20-03** (minor) Commit and dirty on recent-run lines | **Yes.** They were only inside the directory name. | **Accepted** | Each line reads `passed at 01006cf, dirty (…)`; `INVALID` and `MISSING` per invocation; the `reason`; a bold `no ctest invocation recorded`. Both blocks state that these are test-harness runs that do not resolve network-soak failures (KI-35). Report tests 12/12. |
| Codex reviews of PR #20 and of this change (4 passes) | Pass 1, PR #20 branch plus this work: `Hide-LocalPaths` knew only 3 prefixes, so a build directory elsewhere (`D:\external-build`) leaked into `reason`. Pass 2: `ctest_runs[].build` was not sanitized; a valid-then-invalid repeat left invocation 1's file as the "latest". Pass 3: a nested array `[[{…}]]` passed validation through PowerShell member enumeration, and would then crash `check_test_transport.py` and `report.py`. Pass 4: no regression. | **Accepted, all fixed** | `scripts/local-paths.ps1`: known prefixes, then any remaining drive or UNC path becomes `<abs-path>`; 5 direct tests. `build` sanitized per invocation; a whole-record check that is proven to catch a leak (it fails with the sanitizing removed). The top-level copy is removed when a later invocation is invalid or missing; valid-then-invalid test. Each case must be a JSON object; nested-array test, which fails without the check. |
| Answers 1 and 2 | **Agreed.** | — | The hard stop stays, now with the reason persisted. 5 recent runs stays; full history in the immutable run directories. |
| Carry-forward | **Agreed.** | — | KI-35, KI-42 and KI-28b remain unresolved. No protocol or default changes without a separate review. |

---

## Feedback on review #19 (PR #19: review #18 follow-up)

- **Status:** implemented (2026-10-25) on `feature/review19-freshness` (stacked on PR #19); the owner asked for all of it.
- **Source:** [PR #19 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/19) at `01006cf`. ChatGPT reviewed the source and the evidence only.
- **Verdict:** no newly demonstrated blocker; conditional GO for controlled lab work; no merge. The 19 PRs stay stacked pending the owner's integration decision.

| ID | Assumptions verified? | Decision | Done |
|---|---|---|---|
| **FB-19-01** (minor) Run history in the report | **Yes.** `report.py` showed only the selected suite's parent-run status, not each invocation. | **Accepted** | `recent_runs()`: the newest 5 run directories, newest first. Each shows the overall result (bold unless passed) and **every** ctest invocation with its exit code and missing suites. Runs from before per-invocation records are labelled as such. The section appears in PROJECT_STATUS and TEST_RESULTS. Tests: a run with a failed `iter-1` and a passing `iter-2` still shows `iter-1: exit 1`; bounded to 5; non-run names ignored; both blocks contain it. |
| **FB-19-02** (minor) Freshness by timestamp; missing suites | **Yes, and worse than stated.** The previous `test.ps1` accepted a stale file with a newer timestamp as this invocation's result. A suite that wrote no results was silently omitted while ctest exited 0. `-GnsBuild` was also skipped silently when `build-gns/test-results` did not exist. Both were reproduced in the harness. | **Accepted** | `Invoke-Suites` deletes the expected result files **before** ctest, so whatever exists afterwards is this invocation's; no timestamp comparison. An expected suite with no file is recorded in `ctest_runs[].missing`, printed (`MISSING RESULTS`) and fails the run, whatever ctest's exit code. `-GnsBuild` always runs; absent results are missing results. Harness +6 checks (19, then 21 with the positive control): a stale file with a future timestamp is removed and not taken, `missing` is `transport` next to exit code 0, and the run fails. The previous `test.ps1` failed 5 of those checks. |
| Independent review (subagent; Codex hit its account usage limit until 17:35) | Nothing significant; 4 low or medium points, all confirmed by reading. (1) No positive control: a harness or `test.ps1` that failed every run would pass every check. (2) A stale file that could not be deleted (locked by a stuck process) was silently ignored and then taken as fresh. (3) With `-GnsBuild` and no GNS build, the start-failure smoke threw, leaving `run.json` at `incomplete`. (4) An invalid UTF-8 `run.json` (a `ValueError`, not a `JSONDecodeError`) crashed the report. | **Accepted, fixed** | (1) A clean-run positive control: no throw, `passed`, nothing missing, exit 0. 21 checks in all. (2) `-LiteralPath`, and a stale file that is still there after the delete stops the run. (3) The smoke test is skipped and fails the run when the host executable is absent. (4) `ValueError` caught in both readers; tests for invalid UTF-8, broken JSON and `ctest_runs: null`. The locked-file path (2) and the missing-GNS-build path (3) are not exercised by a test. |
| **FB-19-03** (major, status) KI-35, KI-42, KI-28b, KI-32 | **Agreed.** | **Accepted, no change** | They stay open. KI-35 next, with a predeclared investigation of GNS establishment, DTLS and session phases. Tracing stays available. KI-28b and KI-32 are not represented as complete. |
| Answers 1 and 2 | **Agreed.** | — | The top-level files stay a convenience view; `run.json` and `iter-*` hold the history. The bounded recent-runs section is added (FB-19-01). |

---

## Feedback on review #18 (PR #18: review #17 follow-up)

- **Status:** implemented (2026-10-25) on `feature/review18-repeat` (stacked on PR #18); the owner asked for all of it.
- **Source:** [PR #18 review](https://github.com/M0nsT3rG/ProjectMordor/pull/18) at `22ffed1`. ChatGPT reran the 7 report self-tests and compared EXP-027's 12 recovered files with their git blobs (all match). It did not rerun the C++ suites, `test.ps1`, the recovery script or the validator.
- **Verdict:** no new blocking regression; continue lab work; not a merge or release approval. The recovery rule is accepted. The legacy date-named files stay.

| ID | Assumptions verified? | Decision | Done |
|---|---|---|---|
| **FB-18-01** Keep every `-Repeat` iteration | **Yes, from source.** `test.ps1` copied the default-build results once, after the repeat loop. A fail-then-pass left `result: failed` with only the passing details. | **Accepted** | `Invoke-Suites` runs ctest once and immediately copies what **that** invocation wrote to `<run>/iter-<n>/` (GNS: `build-gns-iter-1/`), failed invocations included. `run.json` has `ctest_runs` (build, iteration, directory, exit code, files). The top-level files are the latest invocation's. The transport check runs on every invocation's copy. New self-check `scripts/test-repeat-capture.ps1` (run by `test.ps1`): a stand-in ctest fails, then passes, under a temporary evidence root. 8 checks: both files kept, exit codes `1,0`, aggregate `failed`. It fails on the previous `test.ps1`. Hooks `-CtestCommand`, `-LogsDir` and `-NoSelfCheck` are for that harness only. |
| **FB-18-02** Show run status next to each suite | **Yes.** `report.py` picked suite files without reading `run.json`. | **Accepted** | Each suite carries its run's result, commit and dirty flag: `[run passed, 3ee5a8a, dirty]`, or bold when the run did not pass. A legacy file says it has no run record. Test included. |
| **FB-18-03** The time budget is not a guarantee | **Yes.** Helper start, taskkill, output reading and cleanup have no deadline of their own. | **Accepted** | `wait-bounded.ps1` and `CLAUDE.md` now say "intended budget, about `TimeoutS + DumpTimeoutS` + 5 s, not a strict guarantee, per wait". |
| **FB-18-04** (answer 1) What a creation snapshot means | **Yes.** | **Accepted** | `recover_dated.py` and `CLAUDE.md` say it is the evidence present when the record was committed, not proof that it is exactly the data the record was written from. Uncommitted overwrites are unrecoverable. The existing `provenance.json` files are evidence and are left as written. |
| Code review of PR #19 (`6fef68d`, owner-requested) | 2 findings, both confirmed. (1) `run.json` was rewritten only at the end, so a run that stopped after its suites (an E2E throw, Ctrl+C) kept `incomplete` with an empty `ctest_runs`. (2) Without `[CmdletBinding()]`, a misspelled parameter such as `-LogDir` was silently ignored and the run went into the real `evidence/logs`, as in the red-before incident. | **Fixed** | (1) `Invoke-Suites` writes `run.json` after each invocation. (2) `[CmdletBinding()]`. The harness has 5 new checks (13 in all): a run stopped by E2E on a build without executables keeps `incomplete` with `iter-1:1`; `-LogDir` is refused by name and nothing is written. The early-stop check failed without fix 1. Fix 2 was deliberately not red-checked: without it the typo writes into the real evidence. |
| Answer 2 (legacy files) | **Agreed.** | **Kept** | No deletion. Any later removal would first check consumers and keep a migration index. |
| Next | **Agreed.** | — | KI-35 GNS phases with predeclared comparisons; defaults, gates, hang watcher and the KI-42 assertion unchanged. |

---

## Feedback on review #17 (PR #17: review #16 follow-up)

- **Status:** implemented (2026-10-25) on `feature/review17-evidence` (stacked on PR #17); the owner asked for all of it.
- **Source:** [PR #17 review](https://github.com/M0nsT3rG/ProjectMordor/pull/17) at `3ee5a8a`. ChatGPT reran the comparator reproductions; it did not rerun the PowerShell or C++ suites.
- **Verdict:** FB-16-01..03 addressed; no new blocking regression; continue controlled lab work; not a merge or release approval.

| ID | Assumptions verified? | Decision | Done |
|---|---|---|---|
| **FB-17-01** (answer 1) The 180 s dump limit | **Yes.** The limit is per diagnostic and adds to the wait. | **Accepted** | Documented in `wait-bounded.ps1` and `CLAUDE.md`: one hung process costs at most `TimeoutS + DumpTimeoutS` + 5 s. That bounds each wait, not a battery. The never-returning-helper regression and `hung_dump_timeout` stay. |
| **FB-17-02** (answer 2) Immutable test evidence, recover what was overwritten | **Yes, and wider than stated.** It was not only EXP-027: **20 records** (EXP-004…EXP-028) cited `tests-<suite>-<date>.json`, `e2e-<date>/` or a file in it, which later runs that day rewrote. Every committed version is in git. EXP-027's 9 suite files at `b7405d9` all differed from the current ones. | **Accepted** | **(1) Per-run evidence.** `test.ps1` writes `evidence/logs/tests-<yyyy-MM-dd-HHmmss>-<commit7>[-dirty]/`: suite JSONs (only files written by this run), `run.json` (run id, full commit, dirty flag from a source-only `git status`, builds, repeat, expected transport per suite, E2E exit codes, result; `incomplete` until the end) and `e2e/`. It refuses an existing directory. `check_test_transport.py --dir`; `report.py` reads the newest run directory, with the legacy files still read. **(2) Recovery.** `tools/evidence/recover_dated.py` copies each cited dated path, as it was in each commit that changed the record, to `evidence/logs/recovered/<EXP>/<commit>/` with `provenance.json`. The rule is mechanical: the creating commit first; later edits apart and labelled; identical versions not repeated. 156 files. EXP-023 (two later edits that describe extra verification) and EXP-025 (a text correction) also keep their edit-time versions. The records point at the recovered directory, and the diff only changes those paths. The recovered EXP-027 files are byte-identical to `b7405d9`, and differ from EXP-028's. **(3) Enforcement.** The validator rejects the mutable names, with a test. The legacy files stay, untouched, as the last version of each day. |
| Codex review of this change (working tree, 2 passes) | Pass 1: 2 P2. (a) Files inside `e2e-<date>/` were neither recovered nor rejected: EXP-005 and EXP-009 cite `e2e-2026-10-23/host.log`. (b) `report.py` did not list the new per-run `e2e/` directories. Both confirmed. Pass 2: no actionable regression; recovered files match the git blobs. | **Accepted, fixed** | Both regexes now match the directory or a file in it, and the recovery was redone from the committed records. `report.py` lists `tests-*/e2e`. Tests for both. |
| Next priorities | **Agreed.** | **Accepted, no change** | Next: KI-35 GNS establishment and session phases, with predeclared comparisons. Defaults and gate unchanged; KI-42 strict; hang watcher kept. |

---

## Feedback on review #16 (PR #16: review #15 follow-up)

- **Status:** implemented (2026-10-25) on `feature/review16-followup` (stacked on PR #16); the owner asked for all of it.
- **Source:** [PR #16 review](https://github.com/M0nsT3rG/ProjectMordor/pull/16) at `b7405d9`, with one inline comment. ChatGPT reviewed the source and reran the comparator; it did not rerun the C++ suites.
- **Verdict:** continue controlled lab work; keep the defaults; not a merge or release approval.

| ID | Assumptions verified? | Decision | Done |
|---|---|---|---|
| **FB-16-01** (P2) Bound the diagnostic too | **Yes, from source.** `Wait-Bounded` called `dump-stacks.ps1` synchronously before `Stop-Process`; neither cdb (symbol server, full dump) nor `MiniDumpWriteDump` had a deadline. `hang-watch.ps1` had the same flaw. | **Accepted** | `Invoke-BoundedDump` runs the diagnostic in its own process with `-DumpTimeoutS` (default 180 s). On expiry it kills that process **tree** (cdb included). The target is killed in `finally`. A timed-out diagnostic is recorded as `exit_status: hung_dump_timeout`. `hang-watch.ps1` uses the same helper. `scripts/test-wait-bounded.ps1` (run by `test.ps1`) uses disposable processes only: a diagnostic that never returns and leaves a child, one that finishes, one that fails, and a helper that throws. 12 checks. The two timeout checks failed on the old function and pass now. |
| **FB-16-02** (P2) No verdict from empty or unrecovered cohorts | **Yes, reproduced.** `compare_arms.py <missing dir>` printed `VERDICT: adopt B` with exit 0. `inf <= 0.9*inf` passed condition 2. | **Accepted** | `verdict()` rejects empty input and requires a finite B median. The CLI prints `VERDICT: no decision (no usable runs)` and exits 1 when nothing is usable. 4 regressions; 3 of them failed before the fix. A finite B against a never-recovered A can still be judged. EXP-025/026/027 are unaffected: all their cohorts were finite and populated. |
| **FB-16-03** (KI-40 note) Shutdown on early exits | **Yes, from source.** `mordor-host` and `mordor-client` called `gns_shutdown_for_exit()` only on the normal path; `start_failed` and the `catch` returned without it. | **Accepted** | A local guard declared before the `try` calls it last on every exit path, after the transports are destroyed. The normal path still logs it. `gns_shutdown_for_exit()` traces `killed`/`refused` under `MORDOR_GNS_TRACE=1`. Smoke test in `test.ps1 -GnsBuild`: a GNS host bound to 192.0.2.1 (not on this machine) fails after GNS initialisation, 3 times, and must exit 1 with the shutdown traced (a hang there is dumped through `Wait-Bounded`). Without the guard, the trace was absent. The exception path (`return 2`) is covered by the same guard **by inspection only**; no test drives an exception after GNS initialisation. |
| Independent review of these changes (subagent) | No significant finding; 7 low ones. | **Accepted 1–3, 5, 7; 4 documented; 6 not changed** | 1: the timeout case uses 10 s, so a cold start under load cannot fail it falsely. 2: the start-failure smoke uses `Wait-Bounded`. 3: the throwing-helper case was added. 5: `hang-watch` logs every stream. 7: the `exit_status` comments list `hung_dump_timeout`. 6 (a path ending in `\` breaks the quoting) does not apply to any default path. |
| Codex review of PR #17 (branch diff against `feature/review15-followup`, `3956262`) | No concrete regression found. 11 of the 13 comparator tests ran; the 2 that write temporary files could not run in Codex's read-only sandbox. C++ and PowerShell were checked by inspection. | **Nothing to fix** | The 2 blocked tests are `test_load_turns_missing_recovery_into_inf` and `test_boolean_arm_field`. They passed locally (13/13): an environment limit, not a defect. First Codex review since KI-09 was fixed. |
| KI-35, KI-40 watcher, KI-42 | **Agreed.** | **Accepted, no change** | Defaults and the gate are unchanged. The hang watcher stays in batteries. KI-42 keeps its strict assertion. The next KI-35 step targets the GNS establishment and session phases. |

---

## Feedback on review #15 (PR #15: cross-PR status and next experiment)

- **Status:** implemented (2026-10-25) on `feature/review15-followup` (stacked on PR #15); the owner asked for all of it.
- **Source:** [PR #15 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/15). ChatGPT reviewed the source and the evidence only.
- **Verdict:** conditional GO for lab research; NO-GO for merge, release or untrusted networks.

| ID | Assumptions verified? | Decision | Plan / done |
|---|---|---|---|
| **FB-15-01** (major) KI-35: GNS-only cookie A/B | **Yes, from source.** F-GNS-001: GNS requires an address-bound stateless challenge before a connection exists. **F-GNS-002 (new):** a shared listen socket dispatches packets by source address, so an established connection receives only from the address that answered. We use direct UDP only (`ConnectByIPAddress` / `CreateListenSocketIP`): no relay, no P2P. | **Accepted** | `--dtls-skip-cookie-if-routable` (default off) skips **our** cookie only where the inner transport declares `proves_return_routability` (GNS). Pinning, the invitation binding, budgets and the pending cap are unchanged; UDP always keeps the cookie. Security tests on both backends: UDP still uses the cookie; an unpinned key and a wrong invitation are refused; pending stays capped. A predeclared A/B, EXP-027 (10 + 10 GNS runs): **B not adopted** by the rule. The skip worked (no HelloVerifyRequest; the DTLS phase p50 fell by one RTT, 613 to 413 ms), but the median recovery rose (1992 to 2274 ms) instead of falling 10%; failures were 3 (A) against 1 (B). The cookie stays on; the flag stays off by default. |
| **FB-15-02** (major) KI-42 | **Partly.** A status callback no transport claimed used to vanish silently; the miss itself did not reproduce. | **Accepted** | `MORDOR_GNS_TRACE=1` lifecycle trace (connect, close, forget, every status change, routed or not) and a counter of unclaimed callbacks. A stress test: 180 rounds of endpoint reuse with 3 teardown orders, run 3 times, **0 misses**. All unclaimed callbacks were explained and benign: post-close `→ None` transitions, and the peer's close arriving after our own close in the "both close" order. 5 full-suite runs with the trace: no KI-42. The test is not weakened. |
| **FB-15-03** (major) KI-40 | **Yes.** | **Accepted, root cause found** | Reproduced twice during the KI-42 runs and **dumped** (cdb non-invasive attach). The only thread was in `ExitProcess` → GNS DLL detach → GNS's own `atexit` handler (installed "if static destruction is triggered without cleaning up the library properly") → waiting forever on the GNS global lock, which the already-terminated service thread held. **Fix:** `gns_shutdown_for_exit()` (`GameNetworkingSockets_Kill` when no transport is open, never re-initialised afterwards, KI-27) at the end of the test runner, `mordor-host` and `mordor-client`. 15/15 full transport-suite runs then exited (before: 2 hangs in 6). Regression test for the refusal and no-re-init semantics. Also: the watcher records whether the results were written before the hang, and scripts bound every process wait. |
| **FB-15-04** (minor) KI-41 | **Yes.** | **Accepted** | The contract stays. The 1 failure in 37 is kept in the evidence (KI-41), and success is tested separately at less extreme profiles. |
| **FB-15-05** (minor) test infrastructure | **Yes.** | **Accepted** | `Wait-Bounded`: a process that does not exit is dumped, killed and recorded as `exit_status: hung` (infrastructure), apart from gate failures. Secure E2E has the gate `all_processes_exited` next to `participants_ready`. `run.json` records the *effective* experiment variable. |

**Answers received:**
1. Prefer the GNS-only cookie skip, conditional on the source guarantee and identity staying end to end.
2. The KI-41 contract is right.
3. Investigate KI-42 now.

---

## Reviews of PRs #12–#14 (ChatGPT, 2026-10-24 21:05)

- **Status:** triaged and **implemented** (2026-10-24/25) on `feature/review13-14-followup` (stacked on PR #14), on the owner's go-ahead. Record: EXP-026. Review request #14.
- **As implemented:**
  - **FB-13-01:** `timeline.py --summary` gives phase distributions. 8 more GNS runs were made. The predeclared UDP-only 250 ms experiment was **not adopted**: EXP-025's UDP gain did not replicate.
  - **FB-13-02:** `scripts/hang-watch.ps1` dumps any stuck test process (0 seen). ctest and the runner check that every process exits.
  - **FB-13-03:** ctest pins `MORDOR_TRANSPORT` per suite. `test.ps1` fails on a transport mismatch (`tools/collab/check_test_transport.py`, proven both ways). Aborted runs are counted apart.
  - **FB-14-01:** the smoke test's contract is explicit (KI-41). 30 extra runs were made.
  - **FB-14-02:** the secure E2E readiness barrier plus the host's `--stop-file`; a classified negative was proven; the freshly relinked start-up path was tested.
  - **FB-14-03:** suites ×2 and the full battery at the final head. The repeats fail as before (KI-35).
  - **New:** KI-42, a GNS transport-contract flake, seen once.
- **Sources:** one comment each on PR #12 (P2 accepted, fixed in #14, blockers carried), PR #13 (FB-13-01..03) and PR #14 (FB-14-01..03). Source reviews only.
- **Verdict:** conditional GO for controlled lab work; NO-GO for merge or release. Keep #12 → #13 → #14 stacked.

| ID | Assumptions verified? | Decision | Plan |
|---|---|---|---|
| **FB-13-01** (major) KI-35 | **Yes.** Pooled default-policy failures are 4/14 (GNS) and 2/15 (UDP). The phase breakdown exists per run (`timeline.py`), but not as a distribution. | **Accepted** | Phase distributions across runs (GNS establishment, DTLS authentication, session recovery): `timeline.py --summary`. More independent default GNS runs. A **predeclared transport-specific** experiment: 250 ms on UDP only (GNS keeps 500 ms, where EXP-025 showed no gain), decision rule fixed before the runs. No global timer change, gate fixed. |
| **FB-13-02** (major) KI-40 | **Yes.** It is not reproduced, and not resolved. The watcher lives in the scratchpad, not in the repo. | **Accepted** | Move the hang watcher into the repo (`scripts/hang-watch.ps1`). It dumps through `scripts/dump-stacks.ps1` and records each kill. The battery checks that every process actually exited, not just that its JSON exists. No `Kill`. |
| **FB-13-03** (minor) harness | **Yes.** The plain ctest suites inherit `MORDOR_TRANSPORT` (`CMakeLists.txt:53`); only the `-gns` ones set it. | **Accepted** | Plain suites get `ENVIRONMENT MORDOR_TRANSPORT=udp`. `test.ps1` fails when a suite's JSON reports a transport other than the one registered for it. A check proves a leaked variable is neutralised. Aborted runs are counted separately in `compare_arms.py`. |
| **FB-14-01** (major) KI-41 | **To be measured first** (30 repeat runs with client logs). The client exposes no failure reason today. | **Accepted** | Split the invariant: no crash, no invalid packets and a bounded end state, with `Failed` allowed only with an explicit expected reason, within its deadline, and with transport resources released. Recovery stays tested at less extreme profiles. Record the repeats. |
| **FB-14-02** (major) startup race | **Yes.** `secure-e2e.ps1` gives the host a fixed `--duration-s` and starts clients after `host_started`, so late clients are cut off. | **Accepted** | A barrier: the host runs until a stop file appears (`--stop-file`). The script waits until both clients log `connected` (classified timeout `participants_not_ready`), lets the clients run their window, then stops the host. The client start delay is recorded. The freshly-relinked startup path is tested explicitly. |
| **FB-14-03** (minor) final head | **Yes.** | **Accepted** | Leave, rejoin and linger tests in the suites ×2, and the full v6 battery plus harsh repeats on the final head of this stack, thresholds unchanged. |

---

## Follow-up review of PR #12 (inline, 2026-10-24 19:19)

- **Status:** triaged and **fixed** on `fix/linger-eviction-rejoin` (stacked on PR #13), on the owner's go-ahead.
- **Source:** a review with one inline comment on `src/host/host.cpp:264`, at `5a1ec74`. ChatGPT reviewed the source only, without running anything.

| ID | Assumptions verified? | Decision | Plan / done |
|---|---|---|---|
| **PR12-P2** linger-cap eviction can disconnect a rejoined player | **Yes, confirmed and reproduced.** `on_connect` never removes the peer's `closing_` entry on a rejoin. The cap path closed `closing_.front()` with no active-peer check, unlike expiry (`host.cpp:81`). The same code is on PR #13's branch. A new test (A leaves, rejoins on the same peer, 16 more leave in the window) **failed on both backends before the fix**: on UDP A stopped receiving snapshots, and on GNS A's connection was closed (send `NotConnected`). | **Accepted, fixed** | `Host::accept()` (every successful join, rejoin or retransmitted request) removes that peer's linger entry. The cap path applies the same `find_by_peer` guard as expiry. The test now passes on both backends with 0 evictions. |

**Reservations restated by ChatGPT (unchanged):**
- KI-35 is open: five runs per backend do not establish p95/p99, and GNS ignores the seed.
- KI-40 is observed, not diagnosed: a dump and a lifecycle trace are needed before any init or kill change. `scripts/dump-stacks.ps1` is now ready for that.

---

## Feedback on review #12 (PR #12: GNS readiness gate, seeded v6 repeats)

- **Status:** triaged and **implemented** (2026-10-24) on `feature/review12-retry-experiment` (stacked on PR #12), on the owner's go-ahead. Record: EXP-025. Review request #12.
- **As implemented:**
  - **FB-12-01:** per-attempt ConnectRequest logging and `--reconnect-retry-ms`. The experiment was predeclared, with its analysis tool committed before the runs: 250 ms against 500 ms, 5 seeds per arm and profile. Verdict by the rule: **keep 500 ms**. UDP improved strongly, GNS did not.
  - **FB-12-02:** lifecycle trace at exit, a GNS lifecycle test, and a repro (18 runs plus 10 abrupt or orderly host/client sessions) with a dump-writing watcher. **KI-40 not reproduced.** No `Kill` added.
  - **FB-12-03:** the full battery after the changes passed 20/20 (12 scenarios plus 8 seeded repeats), with realized conditions reported. The pooled failure rate is published (KI-35 open).
  - **FB-12-04:** the GNS UDP challenge was verified from source (F-GNS-001). The cookie cost was measured (about 1 RTT). Nothing removed.
  - **FB-12-05:** 3 more departure tests (repeated lost acks, process end during the linger, two peers leaving and rejoining at once) and a check of the linger bound. No false `leave_confirmed` anywhere.
  - **Found on the way:**
    - the first battery attempt was contaminated by an environment variable left by a failed script (restarted with isolated processes);
    - a second soak stopped after its warm-up (the warm-up stderr is now non-fatal and kept).
- **Source:** [PR #12 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/12). ChatGPT reviewed the source of `5a1ec74` only.
- **ChatGPT's verdict:** conditional GO for further controlled lab testing. NO-GO for declaring KI-35 resolved or for merging.

| ID | Assumptions verified? | Decision | Plan |
|---|---|---|---|
| **FB-12-01** (major) session retry latency | **Yes.** The immediate ConnectRequest on transport `Connected` exists (`client.cpp:194`, review #10). Retries then wait for the fixed `reconnect_retry_us` (500 ms). The host answers duplicate reconnect requests idempotently: the reconnect is counted only if the slot was disconnected (`host.cpp:318`). A duplicate's only cost is re-sending unapplied events, which the client drops. | **Accepted** | Instrument each ConnectRequest attempt (number, time since reconnect start, send result, transport up or not), and when the accept arrives. Then run a **predeclared one-variable experiment**, documented before its runs: reconnect retry 500 ms (A, baseline) against 250 ms (B, the same as the initial connect retry), fixed seeds, both secure harsh profiles, same code. Compare p50, p95, max and failure proportion. Adopt B only if the predeclared criterion holds. No GNS timer or gate change. |
| **FB-12-02** (major) KI-40 exit hang | **Partly.** No debugger or dump tool is installed (`procdump`, `cdb`, `windbg` absent). Installing one needs the owner's approval (rule 6). | **Accepted** | Without installing anything: a lifecycle trace at exit in the test runner (live GNS transports and connections, `main` return, `atexit`, timestamps). A repro loop of the GNS suites with a hang watcher that writes a minidump through Windows' own `MiniDumpWriteDump` (dbghelp), kept under %TEMP% and never committed or uploaded. Symbolized stacks then need a debugger: asked separately. Lifecycle tests: repeated instances, abrupt process exit, orderly close. **No `GameNetworkingSockets_Kill` added.** |
| **FB-12-03** (major) completeness | **Yes.** | **Accepted** | After the changes: the full 12 × 120 s v6 suite with the gates unchanged, plus seeded repeats. Report the failure rate, with the realized conditions per run (delivery ratio, RTT p5/p50) because GNS loss is not seedable. |
| **FB-12-04** (minor) cookie vs identity | **Yes.** The cookie proves return routability only; identity comes from pinned keys and the binding. | **Accepted** | Quantify the cookie's cost per reconnect from the traces (ClientHello → HelloVerifyRequest → second ClientHello). Read the GNS source for whether its connection handshake proves address ownership. GNS is a third-party repository, cloned read-only outside this repo. **No cookie removal in this round.** |
| **FB-12-05** (minor) leave semantics | **Yes.** | **Accepted** | Tests, each also checking that no false `leave_confirmed` happens: repeated lost acks, process termination during the linger, simultaneous leave and rejoin of several peers, peers at the linger cap, cleanup once a lingering peer goes silent. |

**Answers received:**
1. Start with application-level resend scheduling, one variable at a time; do not change gate thresholds.
2. Keep GNS's internal connection timers at their defaults without evidence of a defective setting.
3. Diagnose KI-40 with a dump; no reflexive `Kill`.
4. The leave model is enough for controlled lab use, not a guaranteed graceful exit.

---

## Feedback on review #11 (PR #11: KI-35/KI-37 investigation, gate v6)

- **Status:** triaged and **implemented** (2026-10-24) on `feature/review11-gns-readiness` (stacked on PR #11), on the owner's go-ahead. Record: EXP-024. Review request #11.
- **As implemented:**
  - **FB-11-01:** measured first. The inner GNS connection came up 362–869 ms after the first ClientHello, and the early ClientHellos were queued, not lost (KI-38 partly contradicted). DTLS now starts on the inner `Connected`, and an inner failure is reported (`inner_connect_failed`). Tests: delayed connect with 3 reconnects; failed connect on both backends.
  - **FB-11-02:** the 12-scenario battery at HEAD passed 12/12. The seeded repeats fail 2/5 (harsh-gns-secure) and 1/5 (harsh-udp-secure); KI-35 stays open.
  - **FB-11-03:** at most 16 lingering peers, early close when the client's transport goes, and the leave ends at once when the host leaves first. 6 new edge-case tests.
  - **FB-11-04:** a replayed cookie from another address allocates nothing. Rollover was already tested; the key lifetime is the process. GNS bypass rejected as unverified.
  - **FB-11-05:** `coalesce_records` (off: 10 datagrams in 2 rounds; on: 2), a truncation guard, the MTU-256 case on both backends, and malformed datagrams.
  - **Found on the way:** KI-40 (an exit hang in GNS-build tests); `soak.ps1` had written `gate_version` 5 into `run.json`.
- **Source:** [PR #11 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/11). ChatGPT reviewed the source of `a341969` only.
- **ChatGPT's verdict:** conditional GO for controlled lab development; NO-GO for merge or release (the full v6 battery was not rerun after the review fixes; the last full run was 11/12).

| ID | Assumptions verified? | Decision | Plan |
|---|---|---|---|
| **FB-11-01** (major) GNS handshake readiness | **Yes.** `SecureTransport::connect()` creates the client DTLS session at once (`secure_transport.cpp:97`). The inner `Connected` event is ignored (`:166`), so the first ClientHello can be sent before the GNS connection exists. Whether GNS drops or queues it is not measured (KI-38). | **Accepted** | Measure first: trace the inner `Connected` time next to the first ClientHello. Then, on connection-oriented inner transports (`reports_disconnects`), start the client handshake only on inner `Connected`, with no sleep. An inner failure before that fails the pending session. Tests: immediate connect, delayed connect (latency), failed connect, repeated reconnect. |
| **FB-11-02** (major) full battery | **Yes.** The 12-run battery ran on `04d153a`, before the independent-review fixes. | **Accepted** | The full 12 × 120 s v6 battery at HEAD, plus repeats of harsh-gns-secure and harsh-udp-secure, with no gate change. Each secure harsh run gets a timeline (cookie, flights, inner connect, ConnectRequest/Accept). "Seeded": each process already had its own fixed UDP impairment seed (101/102/103), the same in every run. `soak.ps1 -SeedBase N` now varies them per repeat and records them in `run.json`. GNS fake loss is not seedable; recorded as such. |
| **FB-11-03** (major) departure semantics | **Partly.** `leave_confirmed` already requires the current session id (`client.cpp`, `DisconnectAck` branch). `closing_` holds one entry per clean leave with no upper bound. | **Accepted** | Cap `closing_` (oldest closed first, counted). Drop a peer from it when the transport reports the client gone. Tests: lost first ack, lost last ack, repeated `Disconnect` after slot deletion, host shutdown during a leave, reconnect racing a leave, old PeerId reuse, and an ack for a foreign session ignored. |
| **FB-11-04** (minor) cookie and rate budgets | **Partly.** The HMAC key lives for the process (static, random). Slot rollover accepts the current and previous slot (tested). Invalid cookies count in the stranger or reserved budget. **Not verified:** whether a GNS connection proves return routability. There is no GNS source here, only headers. | **Accepted (checks); GNS bypass rejected for now** | Test that an invalid cookie spends the budget and allocates nothing, and the boundary just after a slot rollover. Document the key lifetime. Keep GNS peers on the same admission path until the GNS handshake is verified from source. |
| **FB-11-05** (minor) flight coalescing | **Yes.** Coalescing changes packetization and loss correlation. | **Accepted** | Add `DtlsConfig::coalesce_records` (default on), to compare against uncoalesced behaviour. Tests: records that would cross the MTU stay in separate datagrams, a fragmented flight at MTU 256, and malformed or truncated datagrams are dropped without harm, on both backends where impairment allows. |

**Answers received:**
1. Coalescing complete records is valid if bounds and parsing are validated on both backends. A DTLS-native approach is preferable long term.
2. Measure, then gate outgoing DTLS on GNS `Connected`; never mask a race with sleeps.
3. Fix the measured causal delay, then judge the distribution over repeated runs.
4. A 1.5 s linger is defensible lab policy. Close earlier on a confirmed client-side disconnect, and keep bounded cleanup for silent peers.

---

## Feedback on review #10 (PR #10: protocol v2 events, soak gate v5)

- **Status:** triaged and **implemented** (2026-10-24) on `feature/ki35-ki37-investigation` (stacked on PR #10), on the owner's go-ahead. Record: EXP-023. Review request #10.
- **As implemented:**
  - **FB-10-01:** `EventAck` bounded by the highest id the transport accepted for that player; larger ones refused whole and counted. Test on both backends.
  - **FB-10-02:** `send`/`transmit` return `SendResult`; an event is marked sent only on `Sent`, otherwise retried with back-off (10 ms doubling to 500 ms) and counted. Test: 300 events through a refusing window, all applied in order.
  - **FB-10-03 (KI-35):** per-attempt traces in both processes, and the timeline tool. Mechanisms shown, then fixed: per-record flight retransmission (coalesced now), and the ConnectRequest waiting for the retry interval. The remaining tail on GNS is decomposed in KI-35 and KI-38. No timer or gate was tuned.
  - **FB-10-04 (KI-37):** protocol v3 leave handshake. It exposed the mechanism: the GNS close overtook the acks, so the host now lingers 1.5 s. On the extra counter: the host releases the slot on the first `Disconnect` it receives, so a host timeout after a leave means no `Disconnect` arrived at all. That cannot be told from a real timeout at the host. The client's `leave_confirmed` (gated in v6) gives that classification instead.
  - **FB-10-05:** gate v6, fixed before its runs.
  - **Answer 5:** stateless cookie gate, with three budgets per second: strangers 64 (shared); each address with an established session 20 (reserved, as answer 5 suggested); cookie-proven ClientHellos 64 (shared). Cookies expire after 30–60 s.
  - **Independent review** (an agent, because Codex fails here, EXP-008) found 4 real defects, now fixed with tests:
    - cookies never expired and bypassed every budget;
    - spoofed floods could deny every first ClientHello;
    - an event that was refused or backing off let later ones overtake it (the client saw 41 gaps in the FB-10-02 test, now 0);
    - repeated `DisconnectAck`s answered any source, with no rate limit.
  - It also found an unbounded leave loop in the client executable (now bounded at 3 s) and minor accounting issues.
  - KI-39, found while verifying: test timing had depended on the machine's timer resolution. Fixed in the test runner.
  - One item is documented, not changed, because gate v6 already ran: the per-client delivery gate misattributes a client that rejoined under a new player id.
- **Source:** [PR #10 comment 6380695083](https://github.com/M0nsT3rG/ProjectMordor/pull/10#issuecomment-6380695083). ChatGPT reviewed the source of `f283a07` only.
- **ChatGPT's verdict:** conditional GO for continued investigation. NO-GO for merge or release (10/12 soaks, KI-35 and KI-37 open).

| ID | Assumptions verified? | Decision | Plan |
|---|---|---|---|
| **FB-10-01** (major) EventAck trust | **Yes, confirmed.** `host.cpp:231` pops every queued event with `id <= applied_through` and never checks the value. An ack beyond the highest id actually **delivered** therefore erases events that were never transmitted (for example ones queued while the player was away), silently. In plaintext mode a forged `EventAck` can do the same. | **Accepted** | Accept an ack only up to the highest id **transmitted on the current connection**. Count and ignore larger ones (`event_acks_invalid`), and never erase untransmitted events. Tests: future ack, ack of an untransmitted event, stale ack, ack across reconnect and restart. The client already acks only delivered ids, after `ack_events_applied()`. |
| **FB-10-02** (major) retransmit bookkeeping | **Yes, confirmed.** `Host::send()` returns `void` (`host.cpp:402`), and `service_events` sets `last_sent_us` (`host.cpp:136`) whatever the transport answered (`Backpressure`, `NotConnected`, `Failed`). | **Accepted** | Return the `SendResult` from `send`/`transmit`. Mark an event sent only on `Sent`; otherwise retry on the next update with bounded back-off, and count it. Test under sustained queue pressure (a tiny transport window). |
| **FB-10-03** (major) KI-35 | **Yes.** The mechanism is unknown. I already lowered the DTLS retransmit cap before identifying it, which is the kind of change ChatGPT warns against. That change was measured and justified, and the gate was not touched, but it is recorded here as tuning done before the mechanism was known. | **Accepted** | A per-attempt timeline in **both** processes, with timestamps: stall exit, timeout notice, old transport close, new connect, each DTLS flight (ClientHello / HelloVerifyRequest / ClientHello with cookie / server flight / client flight / Finished), binding, ConnectRequest/Accept, first snapshot; plus the retransmit timer state and the realized loss per direction. Compare passing and failing traces from separate-process soaks. No further tuning until the mechanism is shown. |
| **FB-10-04** (major) KI-37 | **Yes.** | **Accepted** | Correlate the client's final sends (`SendResult`, GNS connection state, linger, process-exit timing) with the host's GNS callbacks, PeerId mapping and session. Add an **explicit leave handshake**: `Disconnect` → `DisconnectAck`, with the client waiting for it within a bounded time before teardown. Classify "timed out after the client said it was leaving" differently from a real timeout (a new host counter). `Sent` is not proof of receipt. |
| **FB-10-05** (minor) gate validity | **Yes.** The v5 realized checks use a single minimum and an aggregate ratio. | **Accepted** | Report the sample counts and a distribution (p5, p50 RTT, per-client delivery ratio), and require a minimum sample count for the realized gates. Keep them transport-independent. This becomes gate v6, fixed before its runs. |

**Answers received, and what follows from them:**
1. The event semantic is sound for **volatile** effects only. KI-28 stays partially open for durable effects (campaign/Nemesis): a persistent idempotent log or snapshot reconciliation is needed. Re-opened as KI-28b.
2. Keep the three observables, and add sample-count sanity and reproducibility (FB-10-05).
3. and 4. Instrumentation plans as above.
5. **The known-player exemption must not be unlimited:** a UDP source address is not authentication. Give established peers a **bounded reserved budget**: a separate, higher rate (for example 20 new attempts per second per address), with cookie-bearing valid ClientHellos still exempt. A same-address flood then cannot monopolise CPU or allocation, and still cannot starve a cookie-proven reconnect.

## Feedback on review #9 (PR #9: secure provisioning, secure reliable channel)

- **Status:** triaged and **implemented** (2026-10-24) on `feature/secure-liveness` (stacked on PR #9), on the owner's go-ahead ("faz tudo, incluindo o FB-9-02"). Record: EXP-022. Review request #9.
- **As implemented:**
  - **FB-9-01:** liveness only from authenticated records; regression test plus mutation check.
  - **FB-9-02:** protocol v2 application events (D-017), with in-process tests on both backends, plaintext and secure, and executables plus soak v5 gates.
  - **FB-9-03:** soak gate v5, realized impairment.
  - **FB-9-04:** handle-based load, reparse refusal, temp-then-move write; the symlink case was not run here (KI-36).
  - **FB-9-05:** misuse tests, plus ClientHello rate limit and accounting.
  - **Found on the way, fixed:**
    - the rate limit starved a known player's reconnect;
    - head-of-line blocking of `Disconnect` behind reliable `EventAck`s;
    - an event overtaking the `ConnectAccept` stalled the stream on reliable transports.
  - **Open:** KI-35 (secure UDP harsh recovery tail, root cause not established).
- **Source:** [PR #9 comment 6379227966](https://github.com/M0nsT3rG/ProjectMordor/pull/9#issuecomment-6379227966). ChatGPT reviewed the source of `a98b62e` only.
- **ChatGPT's verdict:** conditional GO for lab development only. No merge, no untrusted networks.

| ID | Assumptions verified? | Decision | Plan |
|---|---|---|---|
| **FB-9-01** (major) liveness from unauthenticated datagrams | **Yes, confirmed.** `route()` sets `s.last_rx_us = now_us` (`secure_transport.cpp:339`) for **every** datagram routed to an existing slot, before DTLS decides whether it decrypts. Garbage or replays spoofed from the peer's UDP address keep a vanished peer "alive" forever, which defeats FB-8-04. | **Accepted** | Count liveness only when the established session yields an authenticated record (a decrypted frame, keepalives included). Regression test: the real peer goes silent while malformed, replayed and forged datagrams are injected from its address; it must still reach `idle_timeout`. |
| **FB-9-02** (major) transport ack is not application delivery | **Yes** (KI-28 is open). | **Accepted, as the next milestone** (answer 5) | Application-level event ids, dedup, ack after the game applied the event, and replay. Tests: host restart; disconnect after the transport ack but before application processing; ordering across re-authentication. Selective acks wait. |
| **FB-9-03** (major) configured vs **realized** impairment | **Yes.** The KI-34 defect passed the soak gates because they only bound RTT from above. | **Accepted** | A realized-impairment gate: the measured minimum RTT must be at least about 2 × the configured latency, and the measured loss/duplication must match the configuration within a tolerance. It applies to both handshake and encrypted application data on secure runs, for both backends. This is a new gate version (v5), fixed before its runs. |
| **FB-9-04** (minor) key-file lifecycle | **Partly.** Creation is atomic (`CREATE_NEW` with the DACL set at creation), and temp keys are cleaned in `finally` (scripts) and destructors (tests), also on failure. **But** loading checks the ACL by path and then reopens the file (`secure_identity.cpp:147`): a check-then-open race, and reparse points (symlinks or junctions) are followed. An interrupted write leaves a truncated file that is refused (fail-safe) but blocks re-creation. | **Accepted** | Open one handle with `FILE_FLAG_OPEN_REPARSE_POINT` and refuse reparse points; check the DACL **on that handle** (`GetSecurityInfo`) and read from it. Write to a temp name, then move it into place, so an interrupted write never leaves a half key. Document rotation (new key plus new invitation). Tests: reparse point refused, ACL changed after creation, interrupted write (temp file left, final name absent). No app-bundled DPAPI entropy (answer 3). |
| **FB-9-05** (minor) invitation and authorization | **Yes**, and already the behaviour: possessing an invitation without being on `--allow-peer` is refused (`secure_host_refuses_an_unpinned_player…`, E2E `stranger`). | **Accepted** | More tests: a spoofed or stale invitation, conflicting invitations, swapped fingerprints (the host pins A, B presents A's invitation), and an explicit "the invitation alone grants nothing" assertion. Also rate-limit and count repeated unauthenticated ClientHellos (answer 1). |

**Answers received:**
1. The cookie rule is off-path mitigation only; add rate limits and accounting.
2. Cumulative ack is a prototype layer; prioritise KI-28.
3. DPAPI with a user-only DACL is enough for the lab; no bundled entropy.
4. Keep host-side `--allow-peer`, with only the host identity in the invitation.
5. Do KI-28 next; KI-30 (stateless cookies) is a hard precondition for untrusted UDP.

## Feedback on review #8 (PR #8: secure transport decorator)

- **Status:** triaged and **implemented** (2026-10-24) on `feature/secure-provisioning` (stacked on PR #8), on the owner's go-ahead ("faz tudo"). Record: EXP-021. Review request #8.
- **As implemented:**
  - **FB-8-01:** cookie-aware replacement of pending sessions and a reserved reconnect slot for established players. Test with forged ClientHellos from the player's own address; mutation check.
  - **FB-8-02:** secure `ReliableOrdered` channel inside DTLS (seq, ack, retransmit, dedup, window, exact loss counts), a bounded close drain, and a wire check that only DTLS records leave a secure client.
  - **FB-8-03:**
    - `mordor-keygen` and DPAPI key files with a user-only DACL;
    - public-only invitations;
    - `--secure-key` / `--invitation` / `--allow-peer` with fail-closed validation;
    - negative tests;
    - multi-process secure E2E and secure soak on both backends.
  - **FB-8-04:** idle timeout with keepalives; documented semantics.
  - **FB-8-05:** KI-27 wording stays empirical; reported upstream as GNS#458.
  - **Found on the way:**
    - **GNS adapter:** messages received before a peer close were dropped;
    - **GNS adapter:** status callbacks piled up while the runtime outlived its instances;
    - **decorator:** messages that arrived in the same poll as a disconnect were dropped;
    - **DTLS** sent with `now=0`, so UDP-simulated latency and jitter were never applied to DTLS traffic (KI-34). This means earlier UDP DTLS latency claims (EXP-018/019) and the first secure UDP soaks are invalid for latency; the claims are corrected;
    - once latency was real, **whole-window reliable bursts livelocked** against DTLS's 64-record replay window, so in-flight is now capped at 32 (KI-32).

    All five are fixed and tested.
- **Source:** [PR #8 comment 6377503056](https://github.com/M0nsT3rG/ProjectMordor/pull/8#issuecomment-6377503056). ChatGPT reviewed the source of `6551ab0` only.
- **ChatGPT's verdict:** conditional GO for isolated development. NO-GO for public networks, or for merging as a completed security feature.

| ID | Assumptions verified? | Decision | Plan |
|---|---|---|---|
| **FB-8-01** (major) pending sessions vs a live player | **Yes, and more concrete than stated.** While a pending session exists for an address, every new ClientHello from that address goes into **it** (`secure_transport.cpp:194–195`). A forged ClientHello therefore holds the only pending slot, and a legitimate reconnect from the same address is **delayed by up to `pending_lifetime_us` (10 s)**. The established session keeps receiving records. | **Accepted** | Let a new ClientHello with a different client random replace an **unauthenticated** pending session, rate-limited, never touching the established one. Tests: forged and valid reconnects at the same time; cap exhaustion while a player is active; expiry; old records; the established session stays usable until a promotion; event order (`Disconnected(replaced)` before `Connected`). |
| **FB-8-02** (major) no secure reliable class | **Yes.** `ReliableOrdered` returns `Unsupported` through the decorator. Host/Client then fall back to their unreliable ×3 paths, which are still encrypted. No plaintext bypass: the inner transport is reachable only through the decorator. | **Accepted** | Secure consequential events: application event ids, ack and replay over the authenticated channel (ties to KI-28). Until then, no "transport parity" claim. Add a test that no plaintext `ReliableOrdered` reaches the wire. |
| **FB-8-03** (major) operational integration | **Yes** (KI-31). | **Accepted** | Key files in the user profile protected by a user-only ACL, with DPAPI for the private key (answer 3). An invitation string carrying only public fields: session id, expiry, fingerprints. Secure-only startup with no fallback. Negative tests: missing, wrong or unreadable key; stale invitation. Secure multi-process E2E on both backends. No secrets in logs, command lines, environment variables or Git. |
| **FB-8-04** (minor) capability contract | **Yes.** On UDP the decorator reports the disconnects it **detects** (DTLS rejection, expiry, replacement, inner loss) but does not detect a silent peer. Host and session liveness still come from the session timers. | **Accepted** | Document `reports_disconnects` precisely ("reports peer loss it detects; silent loss needs the session's liveness"), or add a secure-peer idle timeout with a test. |
| **FB-8-05** (minor) KI-27 wording | **Yes.** | **Accepted** | Already "empirically mitigated". The refined upstream draft separates logged results from console-only ones and labels the cause as a hypothesis. New discriminating evidence: only a re-`Init` after `Kill` triggers it (0/500 first-Init processes, 18/200 second-Init processes, logged). |

**Answers received:**
1. A new PeerId plus `Disconnected` is appropriate; test the event order.
2. The back-off is plausible; benchmark it under real jitter and outages.
3. User-profile files with a user-only ACL, DPAPI for the private key, public-only invitations, mutual confirmation.
4. Consider a stateless cookie before public exposure.
5. Owner review before posting upstream.

## Feedback on review #7 (PR #7: DTLS over the Transport)

- **Status:** triaged and **implemented** (2026-10-24) on `feature/secure-transport` (stacked on PR #7), on the owner's go-ahead. Record: EXP-019. Review request #7.
- **As implemented:**
  - **FB-7-01:** caps and expiry in the decorator; the stateful cookie is labelled (KI-30).
  - **FB-7-02:** `DTLS_set_timer_cb` back-off, plus timer tests.
  - **FB-7-03:**
    - KI-27 is open;
    - a minimal reproducer (`tools/ki27-repro`) and a 10-process stress test;
    - upstream draft `docs/upstream/gns-ki27-issue.md`, not published.
  - **FB-7-04:** reply rate limit, plus binding tests.
  - **FB-7-05 / answer 3:** `SecureTransport` decorator (D-015), with Host/Client over DTLS and no plaintext fallback.
- **Source:** [PR #7 comment 6376769358](https://github.com/M0nsT3rG/ProjectMordor/pull/7#issuecomment-6376769358). ChatGPT reviewed the source of `e81065d` only.
- **ChatGPT's verdict:** conditional GO for isolated development. Not for WAN, release or merge.

| ID | Assumptions verified? | Decision | Plan |
|---|---|---|---|
| **FB-7-01** (major) cookies are stateful | **Yes.** A `DtlsSession` (with `SSL_CTX`, `SSL`, BIOs and the pin) is built for every new peer before any cookie, and `make_cookie` hashes the allocated `PeerId`. That makes it a handshake-integrity measure, **not** a stateless DoS gate. | **Accepted** | Label it "stateful cookie" everywhere. Bound the pre-authentication state (pending sessions per peer and in total, bytes, lifetime) with tests. A truly stateless front door (`DTLSv1_listen` or an equivalent before allocation) stays a precondition for any untrusted network. |
| **FB-7-02** (major) timer driving | **Partly.** `service()` calls `DTLSv1_handle_timeout` only once the remaining time is 0. A late `service()` call therefore still fires (the timer has expired), and each flight re-arms OpenSSL's own back-off. **But nothing tests** delayed scheduling, several retransmitted flights, a long outage, or the absence of a busy loop. | **Accepted** | Seeded tests: a delayed or irregular `service()` cadence; a long-loss window during the handshake (must end in success or `handshake_timeout`, with bounded retransmits); a check that retransmissions follow OpenSSL's back-off (no busy loop). OpenSSL's `DTLSv1_get_timeout` semantics are the source of truth. |
| **FB-7-03** (major) KI-27 | **Yes: I overstated it.** The probe localises the trigger, and 8 → 0 is an empirical result, not proof. Process-lifetime state has its own isolation costs. | **Accepted, applied now** | KI-27 is set back to **Open (empirically mitigated)** and the release-safety wording is removed. Stress tests: longer probes and Init/close loops across processes. A minimal reproducer plus the exact assert text, ready for an upstream issue. **Filing it publicly needs the owner's approval.** |
| **FB-7-04** (minor) binding robustness | **Partly.** A wrong pinned key and id/version/expiry mismatches are already tested. Not tested: an altered confirmation, an out-of-order or duplicate binding, expiry **during** a long handshake, response traffic under replay. On the last point, DTLS's replay window already drops replayed records before `read_records`, so a reply goes out only for a genuinely new record (1:1). There is still no rate limit. | **Accepted** | Rate-limit binding replies (at most one per `binding_resend_us`). Tests: altered confirmation (test hook), duplicate/reordered binding under impairment, invitation expiring mid-handshake, and a bound on binding replies under loss and reordering. |
| **FB-7-05** (minor) pending-session lifecycle | **Yes, and worse than stated:** the pending-then-promote rule exists **only in the test harness** (`tests/dtls/dtls_transport_tests.cpp`). The library offers only `dtls_is_client_hello`. | **Accepted** | Move it into a library component: the **secure transport decorator** (answer 3), which implements `Transport` over a backend. It handles a cap of one pending session per peer and a total cap, expiry, promotion only when authenticated, retirement of the old session at promotion, and dropping of old-session records after promotion. Tests: several forged ClientHellos, parallel valid reconnects, same and new invitation, old application records after promotion. |

**Answers received, and what follows from them:**
1. Check both pinned identities and the invitation fields: identity is already enforced by DTLS/DANE; add the explicit test.
2. Pending-then-promote is defensible if bounded and tested (FB-7-05).
3. Use a **secure transport decorator** that gates Host/Client traffic, with no plaintext fallback.
4. Pre-cookie allocation is acceptable only for strict loopback/lab use, with bounds (FB-7-01).
5. Keep the workaround documented, file upstream, and keep KI-27 open (FB-7-03).

## Feedback on review #6 (PR #6: GNS hardening and DTLS proof)

- **Status:** triaged 2026-10-24 and **implemented** on `feature/dtls-transport` (stacked on PR #6), on the owner's go-ahead. Record: EXP-018. Review request #6.
- **As implemented:**
  - **FB-6-01:** fail-closed binding in `DtlsSession` (exporter confirmation over id, version and expiry). Tests cover each mismatched field, a missing confirmation and an expired invitation.
  - **FB-6-02:** DTLS over `Transport::Unreliable` on both backends with OpenSSL timers, plus loss/jitter/reorder/duplication, reconnect and MTU tests. GNS reliable delivery is not used (decided).
  - **FB-6-03:**
    - root cause localised by probe and source reading;
    - mitigated with a process-lifetime runtime and a 50 ms wait after the first Init;
    - regression gates added;
    - no upstream report filed.
  - **FB-6-04:** wording sweep (`transport.hpp`, `protocol.md`, KI-26).
  - **FB-6-05:** `scripts/asymmetric.ps1` with separate processes, passed on both transports.
  - **Answer 3:** `--connect-deadline-s` bounded to [1, 120] s.
  - **Answer 5:** HelloVerifyRequest cookies on both backends; statelessness is not done (KI-29).
- **Source:** [PR #6 comment 6376091101](https://github.com/M0nsT3rG/ProjectMordor/pull/6#issuecomment-6376091101). ChatGPT reviewed the source of `1fc796f` only.
- **ChatGPT's verdict:** conditional GO for isolated testing. Not approval for untrusted networks or for merge.

| ID | Assumptions verified? | Decision | Plan |
|---|---|---|---|
| **FB-6-01** (major) session binding | **Yes.** `dtls12_exporter_binds_invitation_and_is_fresh_per_handshake` only shows that the exporter values match when both sides choose the same string. Nothing exchanges or verifies the value, so peers holding different invitations still complete DTLS. The design (`authentication-design.md` §4a, "key confirmation… verified on both ends") is not implemented. | **Accepted** | A binding message inside the channel: each side sends a confirmation of the exporter over (invitation id, protocol version, expiry). It fails closed on mismatch, expiry or absence before any game message. Tests deliberately mismatch the id, the version and the expiry, and cover a missing confirmation. |
| **FB-6-02** (major) DTLS reliability evidence | **Yes.** The in-memory shuttle never loses, delays or reorders a datagram, so `DTLSv1_get_timeout`/`DTLSv1_handle_timeout`, retransmission and MTU fragmentation are not exercised. | **Accepted** | A DTLS layer over `Transport::Unreliable` on **both** backends, driven by OpenSSL's own timers (answer 4). Tests: mutual identity, handshake under loss, jitter and reorder, replay, reconnect = new handshake. GNS reliable delivery is **not** used to carry DTLS (decided and documented, as asked). |
| **FB-6-03** (major) KI-27 GNS assertion | **Yes.** It is a sanity check in `PollRawUDPSockets` (`steamnetworkingsockets_socketthread.cpp:2605–2609`): `Assert(pSock->m_callback.m_fnCallback)` over every raw socket. `CRawUDPSocketImpl::Close` sets the callback to null and **defers** the deletion (`ProcessPendingDestroyClosedRawUDPSockets`), so the assert fires if the service thread polls a closed socket before its cleanup runs. `DBGFLAG_ASSERT` is defined "even in release" (`tier0/dbg.h`), which is why we see it. **Hypothesis from the source, not proven:** a GNS-internal teardown-ordering race, triggered by our tests closing sockets while the service thread runs. | **Accepted.** It is not dismissed. | Capture every occurrence with the test name. Reproduce in isolation (an open/close loop), check whether our call pattern (closing while connections linger, `Kill` right after) is the trigger, and try a mitigation such as flushing callbacks before closing. Release suitability is gated on a root cause or a documented upstream cause, with a regression test. |
| **FB-6-04** (minor) guarantee wording | **Yes.** | **Accepted** | Sweep the docs and logs so that `Sent` is never described as delivery. KI-28 stays explicit. |
| **FB-6-05** (minor) coverage gaps | **Yes.** The 14 + 5 skips stay visible in `docs/test-coverage.md`. | **Accepted** | Asymmetric impairment on GNS with separate processes: an E2E/soak-style scenario where only one client process is impaired. |

**Answers received, and what follows from them:**
1. Sharing an identical config is fine if refcount and reset are tested (they are).
2. The counters are fine if explicitly labelled.
3. Make the 20 s deadline a configurable policy with an explicit maximum, plus tests: CLI `--connect-deadline-s`, clamped, with a test.
4. Run DTLS over the unreliable datagram transport with OpenSSL's own retransmission (matches FB-6-02).
5. Use stateless cookies (`DTLSv1_listen`) on raw UDP listeners, and do not treat GNS's handshake as a DoS or identity check either.
6. Investigate the assertion (FB-6-03).

## Feedback on review #5 (PR #5: GNS adapter)

- **Status:** triaged 2026-10-23. **Implemented** 2026-10-24 on `feature/gns-hardening` (stacked on PR #5), on the owner's go-ahead, which also approved the FB-5-05 proof test. Record: EXP-017. Review request #5.
- **As implemented:**
  - **FB-5-01:**
    - the counter semantics are fixed in `transport.hpp`; GNS `close()` counts connections in `closed_with_unacked_reliable`;
    - test `contract_mid_backlog_disconnect_then_reconnect_is_clean`;
    - application-level delivery is deferred to the reliable-events milestone (KI-28).
  - **FB-5-02:**
    - `connect_deadline_us` at the session layer, with a timeout table in D-014;
    - 4 lifecycle tests.
  - **FB-5-03:**
    - the guard (refuse a different config, join the same one, last holder resets) and `impairment_refused`;
    - test `contract_process_global_impairment_is_guarded_and_never_leaks`.
    - **It exposed a false GNS pass:** `timeout_notice_recovery_under_jitter_reconnects_once`, now UDP-only.
  - **FB-5-04:**
    - 8 `_over_transport` substitutes;
    - `docs/test-coverage.md`.
  - **FB-5-05:**
    - `tests/dtls`, 8/8 after a failed first run (DANE must be enabled);
    - see `docs/d013-evaluation.md`.
- **Source:** [PR #5 comment 6375464043](https://github.com/M0nsT3rG/ProjectMordor/pull/5#issuecomment-6375464043). ChatGPT reviewed the source of `9ae2b2a` only.
- **ChatGPT's verdict:** conditional GO for isolated lab work. Not production-ready, and no untrusted access.

| ID | Assumptions verified? | Decision | Plan | Verification planned |
|---|---|---|---|---|
| **FB-5-01** (major) reliability accounting | **Yes, confirmed in the code.** `Conn::reliable_outstanding` is one boolean per connection, cleared when the aggregate `m_cbPendingReliable`/`m_cbSentUnackedReliable` reach 0. It is wire-level only and says nothing about the peer application. GNS `discarded_on_close` counts **connections** (`gns_transport.cpp:165`), while UDP counts **messages**, so the same counter has two meanings. Also: if the last instance closes, `release()` calls `GameNetworkingSockets_Kill()`, which can end lingering connections early. | **Accepted** | (a) Document the transport counters as wire-level diagnostics, not proof of delivery. Give GNS a separate per-connection counter so `discarded_on_close` keeps one meaning. (b) Application-level delivery (event ids, app ACK, resume or snapshot replay) becomes part of the reliable-events milestone: there are no consequential events yet. (c) Add disconnect-mid-backlog and reconnect tests. | Contract tests for the counter semantics; mid-backlog tests on GNS. |
| **FB-5-02** (major) lifecycle and timeouts | **Partly confirmed.** The initial connect is bounded only by GNS's `TimeoutInitial` (10 s) plus our 5 s window; our own timer is skipped until transport `Connected` (`client.cpp:113`). If GNS never reported `Disconnected`, nothing would bound it: there is **no overall session deadline**. Reconnect is bounded by the 15 s window. No test covers a black-holed connect, no listener, or a close during the handshake. | **Accepted** | Add an overall connect deadline at the session layer that covers transport and session attempts. Document which layer owns each timeout. Add tests: no listener, black-holed connect, remote close during handshake, repeated reconnects, host restart (the last already exists). | New tests on both backends. |
| **FB-5-03** (major) process-global impairment | **Yes, confirmed.** Any instance's `set_impairment` overwrites the global config and takes ownership (`gns_transport.cpp:353–357`). An owner's `close()` resets impairment for every other instance. The single-owner rule is not enforced. | **Accepted** | Enforce a single owner: a second impairing instance gets an explicit error, never a silent overwrite. Add a leakage test. Run asymmetric GNS scenarios in **separate processes** (answer 6). | Leakage and guard tests; separate-process asymmetric test. |
| **FB-5-04** (minor) test completeness | **Partly.** Each of the 18 skips already states its reason in the code, and our reports gave skip counts. But 11 skips use a raw UDP fake peer, and several of them (malformed messages, foreign session, timeout-notice replay) test **our** protocol checks, which could run over a GNS connection carrying bad payloads. There is no coverage matrix. | **Accepted** | A coverage matrix (`docs/`) per test × backend, naming the capability behind each skip. GNS substitutes that send malformed or replayed protocol payloads through a real GNS connection. | Matrix plus new GNS tests. |
| **FB-5-05** (minor) D-013 | **Yes.** Our evaluation already required proof tests (condition 2), but it rests on header availability only. | **Accepted** | A minimal two-peer DTLS 1.2 + RFC 7250 proof test covering a correct pinned key, a wrong key, session binding and downgrade rejection. It links OpenSSL directly, so **it needs the owner's approval**: no new crypto dependency without it. | The proof test, if approved. |

**Answers received:**
1. Test changes are acceptable only where a backend capability differs, with a substitute invariant.
2. The counters are diagnostics; use app ACK and replay for events.
3. The 500 ms drain is a policy, not a guarantee.
4. Keep an overall deadline.
5. DTLS + RPK is plausible but unproven.
6. Use separate processes.

## Recommendation after review #4 (GNS adapter, priorities 1–3)

- **Status:** implemented on `feature/gns-adapter` (stacked on PR #4), 2026-10-23. Experiment record: EXP-016. Review request: [CHATGPT_REVIEW_REQUEST.md](CHATGPT_REVIEW_REQUEST.md) #4.

| Item | Decision | Implemented in | Verification |
|---|---|---|---|
| **P1** GNS adapter behind `Transport` | **Accepted** | `src/network/gns_transport.*`; `--transport gns`. ReliableOrdered and Unreliable; `PeerId` per connection; Connected/Disconnected events; reconnect = new connection (D-014); `LimitExceeded` → `Backpressure`; GNS-native impairment; host logs transport counters. | Contract suite 15/15 on both backends. |
| **P1** no silent drop of accepted reliable data | **Accepted** | The adapter tracks unacknowledged reliable data itself (GNS stops reporting it after a failure): `accepted_then_failed`, `closed_with_unacked_reliable`; bounded 500 ms drain on `close()`. | `contract_disconnect_with_pending_reliable_is_reported`, `contract_close_peer_with_unacked_reliable_is_counted` (the first failed on the first run, fixed). |
| **P2** existing tests unchanged on both | **Accepted with documented exceptions** | UDP assertions are unchanged. Changes, each with a reason in the code: (a) a null dereference in a contract test became a FAIL (it crashed); (b) `endpoint_reuse` no longer assumes a closed `PeerId` is reusable on connection-oriented backends (UDP branch keeps the exact old assertion); (c) exact loss counts and exact discard counts are asserted only where the backend declares `exact_loss_accounting`; (d) 18 tests needing raw UDP or asymmetric impairment report **SKIPPED** on GNS (process-global impairment, KI-24), never PASS; (e) the jitter test couples sim time to real time under GNS (`step_wait`, a no-op on UDP). | UDP: unit 15, integration 26, network 12, transport 15. GNS: transport 15, integration 12 P / 14 SKIP, network 8 P / 4 SKIP. ×2. First GNS runs preserved: 7/10, 9 P / 3 F, 6 P / 3 F. |
| **P2** new integration scenarios | **Accepted** | Two clients; reliable ordering and dedup under loss both directions; interleaved reliable/unreliable; backpressure; disconnect with pending reliable; symmetric outage and reconnect; loss/latency/jitter/reorder via the soak profiles. | 5 new contract tests, 1 new integration test, soak gate v4 on both transports (43/43 × 6). |
| **P2** per-transport results | **Accepted** | Gate v4 (transport declared and matched to the host log; thresholds unchanged); separate `tests-*-gns-*` logs; `report.py` lists GNS suites separately. | `docs/soak-testing.md` gate v4 table; two failed GNS soaks preserved. |
| **P3** D-013 | **Evaluated; no dependency added** | `docs/d013-evaluation.md`: recommend DTLS 1.2 (OpenSSL 3.6.5) with RFC 7250 raw public keys pinned by fingerprint over `Unreliable` on both backends; invitation id and version bound via exporter. Fallback: snow. noise-c not recommended (no audit found). | Owner decision pending. Loopback or trusted lab only until then. |

**Problems found while implementing (not raised by the review):** 8 defects, all in EXP-016. The most important: the client ignored a transport `Disconnected` while already reconnecting (it retried a dead connection forever); global GNS impairment outlived its owner; a clean leave became a host timeout because the unreliable `Disconnect` was lost at close.

## Feedback on review #4 (PR #4: transport interface)

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #4 comment 6373680146](https://github.com/M0nsT3rG/ProjectMordor/pull/4#issuecomment-6373680146). ChatGPT reviewed the source only.
- **ChatGPT's verdict:** conditional GO to continue the adapter work. Not approval to merge or to expose the prototype.
- **Implemented on:** `feature/transport-interface`. Experiment record: EXP-015.

| ID | Assumptions verified? | Decision | Implemented in | Verification |
|---|---|---|---|---|
| **FB-4-01** (major) | **Yes.** v1 `send()` was unreliable-only, and the design's `Delivery` classes were missing. | **Accepted** | Contract v2: `Delivery::{Unreliable, ReliableOrdered}`, `TransportCapabilities::reliable_ordered`, and an explicit `SendResult::Unsupported`. A backend never silently downgrades. UDP reports `reliable_ordered=false`. | `contract_reliable_delivery_is_provided_or_explicitly_unsupported`: UDP returns `Unsupported` and sends nothing. GNS must return `Sent` once its adapter exists. |
| **FB-4-02** (major) | **Yes.** Endpoint-only identity cannot express connections, reuse or reconnect. | **Accepted** | `PeerId{id, generation}`: ids are never reused, and the generation increases when an address comes back. Events: `Connected` (always before the first message), `Disconnected`, `Message`, `DroppedInbound`. Host slots and the client's host link store a `PeerId`. A transport `Disconnected` puts the slot into grace (host) or reconnects (client), so the transport can own liveness where it detects it. | `contract_endpoint_reuse_gets_new_peer_generation`, `contract_two_simultaneous_peers_are_routed_separately`, `contract_connected_precedes_first_message_and_bytes_roundtrip`. Disconnect-with-queued-reliable cannot exist on UDP (no reliable class), so it is required for the GNS adapter. |
| **FB-4-03** (minor, security design) | **Yes.** `crypto_kx` is static–static, with no forward secrecy, transcript binding or key confirmation. | **Accepted.** I withdrew my libsodium proposal. | `docs/authentication-design.md` §4a: a precise state machine (Idle, Handshaking, Authenticated, Rejected, Closed); transcript binding; two-way key confirmation; per-direction 64-bit nonce counters that are never reused; a 1024-record replay window; no game message before authentication; re-authentication on every reconnect. D-013 options revised: **TLS 1.3/DTLS via OpenSSL with pinned certificates**, or **Noise IK/XX** via a reviewed implementation; `crypto_kx` plus our own handshake rejected. | Design only. D-013 still needs the user. |
| **FB-4-04** (minor, tests) | **Yes.** Failure semantics were undefined. | **Accepted** | `SendResult::{Sent, Unsupported, NotConnected, TooLarge, Backpressure, Closed, Failed}`; `TransportStats` counts `accepted_then_failed`, `discarded_on_close`, `simulated_drops`, `dropped_inbound_oversized`. Host and client count every non-`Sent` result (`send_errors`), and the host logs the transport counters at stop. | 10 contract tests (new suite `tests/transport`). **Mutation check:** I removed the generation increment and the discard counting, and exactly the two matching tests failed (8/10); restored, 10/10. The moderate soak reports `send_errors=0`, `accepted_then_failed=0`, `discarded_on_close=0`. |
| **FB-4-05** (suggestion) | **Yes.** | **Accepted** | The PR #4 description is reworded: "UDP transport refactor + contract + GNS test hardening". **The GNS host/client is not integrated.** The spike results stay classified separately (EXP-013/014). | n/a |

**UDP compatibility:**
- **Tests:** the UDP test and harness files are unchanged against `42e9451`, and all pass ×3 in both builds.
- **E2E:** passed.
- **Moderate soak, gate v3:** 41/41.

**A mistake of mine caught during implementation:** my rewrite of `Host::update` briefly polled the transport a second time after the timeout checks, which changes the semantics. I caught it before compiling and added `Transport::flush()`, so the original order (receive, then flush) is kept.

---

## Feedback on review request #3 (GameNetworkingSockets spike)

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #3 comment 6373375242](https://github.com/M0nsT3rG/ProjectMordor/pull/3#issuecomment-6373375242). ChatGPT reviewed the source only.
- **ChatGPT's verdict:** conditional GO for implementing the transport interface and further isolated testing.
- **Implemented on:** `feature/transport-interface` (stacked on #3). Experiment record: EXP-014.

| ID | Assumptions verified? | Decision | Implemented in | Verification |
|---|---|---|---|---|
| **FB-3-01** (major, security) | **Yes, and stronger than stated.** In the GNS 1.6.1 open-source build, `IP_AllowWithoutAuth` and `IPLocalHost_AllowWithoutAuth` **default to 2**, so unauthenticated peers are accepted on any IP without warning. Source: `csteamnetworkingsockets.cpp` under `STEAMNETWORKINGSOCKETS_OPENSOURCE`. Trusted-CA loading (`CertStore_AddCertFromBase64`) is internal and not in the public headers. | **Accepted.** The negative baseline is now asserted. The positive-identity, wrong-identity and MITM tests are **planned against the app-layer design**, because GNS native certificates are not usable through the public API. | `tests/gns`: `gns_default_connection_security_flags` asserts encrypted **and** `unauthenticated` on both ends; new `gns_opensource_build_accepts_unauthenticated_peers_by_default` pins the default at 2, so a library update that changes it is caught. The design is in `docs/authentication-design.md`. | Both pass, ×3 and in 3 parallel instances. The prototype stays loopback and trusted-LAN only. |
| **FB-3-02** (major, architecture) | **Yes.** The interface was design-only. | **Accepted, in the recommended order.** The UDP adapter is done; the GNS adapter is next. | `net::Transport`, `UdpTransport`, `make_transport`; Host and Client now hold `std::unique_ptr<Transport>`; `ImpairedSender` takes any transmit function. | **No UDP test or test-harness file changed** (`git diff 42e9451 -- tests/unit tests/integration tests/network tests/test_support.hpp tests/test_main.cpp` is empty); only `tests/gns/` was rewritten, deliberately, for FB-3-01/03/04/05. Unit 15, integration 25 and network 12 pass ×3 in both builds. E2E passes. A lan soak at gate v3 gives 41/41, with RTT 10.5/12.1 ms and recovery 21 ms, the same as before the refactor. |
| **FB-3-03** (minor, tests) | **Yes.** Traffic was sequential, with one pair. | **Accepted at spike level.** Disconnect-mid-backlog and reconnect: **deferred to the adapter**, where they exercise our session layer. | New GNS tests: `gns_concurrent_bidirectional_reliable_ordered` (3000 each way, simultaneously); `gns_two_simultaneous_clients`; `gns_interleaved_reliable_and_unreliable_under_loss`; `gns_send_queue_pressure`. | All pass. Interleaved, 10% loss: reliable 1000/1000 exact; unreliable 900/1000, no duplicates. Queue pressure: 89 661 accepted, **110 339 refused** (explicit backpressure), every accepted message delivered in order. GNS logged "recv queue overflow", so the adapter must drain the receive queue frequently. |
| **FB-3-04** (minor, tests) | **Yes** (TOCTOU). | **Accepted** | `Net` retries the listen up to 8 times with a fresh probed port. | **3 test processes in parallel: 21/21 passed.** The probe stays a test-only mechanism; production code will take an explicit port. |
| **FB-3-05** (minor, isolation) | **Yes.** Callback state and fake config were process-global with no cleanup on throw. | **Accepted** | `GnsLib` RAII resets fake loss, lag and reorder and the global callback even when a CHECK throws. The callback routes through a per-listen-socket registry instead of a global connection handle. | Passes ×3 and in parallel. |
| **FB-3-06** (suggestion, comparability) | **Yes.** | **Accepted: this changes my plan.** I had been about to put `ImpairedSender` *above* GNS. Instead the GNS adapter will use GNS's native fault injection; `ImpairedSender` stays for raw UDP. Both use the same target profiles; realized conditions are logged; gates get transport-labelled versions. No cross-transport latency claims without matched observations. | Recorded in D-012 and `docs/transport-interface.md`. | n/a until the adapter exists |

**Answers adopted:**

- **Answer 1 (authentication).** Pinned per-player public keys, exchanged out of band and bound to a session invitation, with explicit fingerprint confirmation. Verified against the real library: GNS cannot provide this through its public API, so the design puts end-to-end authenticated encryption at our protocol layer, using a vetted library (libsodium proposed). **Library choice D-013 is pending with the user.**
- **Answer 2 (session layer).** On GNS, GNS owns transport liveness and reconnect. Game identity, slots and grace stay in the application. A reconnect means a new transport connection plus re-authentication.
- **Answer 3 (comparability).** See FB-3-06.
- **Answer 4 (integration order).** Followed: interface plus UDP first, then the GNS adapter. GNS stays default OFF.

---

## Follow-up verification of review #2

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #2 comment 6372799837](https://github.com/M0nsT3rG/ProjectMordor/pull/2#issuecomment-6372799837). ChatGPT verified the source only.
- **ChatGPT's verdict:** no new blocking correctness defect; conditional acceptance for continuing Phase 2A lab work.

| Item | Assumptions verified? | Decision | Implemented in | Verification |
|---|---|---|---|---|
| **FU-2-1: manifest uniqueness, names and cardinality; match the chosen scenario** | **Yes.** v2 checked only for a non-empty list. A duplicate (`client1, client1, client2`) produced duplicated per-client gates, and a self-consistent but different manifest was trusted. | **Accepted** as gate **v3** | `run.json` names a `scenario`, and `analyze.py` has a fixed `SCENARIOS` table. New gates: `manifest_names_valid` (`^client[1-9][0-9]{0,2}$`), `manifest_names_unique`, `scenario_known`, `manifest_matches_scenario`, `stall_plan_matches_scenario`. Each client is gated once even if listed twice. | 6 new self-checks: duplicates; invalid names (`../host`, empty, the integer `7`, `client0`); unknown or missing scenario; a self-consistent one-client manifest that differs from the scenario; stall-plan mismatch; window rates. **Writing them exposed a defect in the new code:** a non-string name crashed the analyzer (`TypeError` in `sorted`) instead of failing a gate. Fixed. Self-checks: **17/17**. |
| **FU-2-2: bandwidth gate is a run-wide mean, not burst/peak** | **Yes.** | **Accepted**: relabelled. Peak gate **deferred** with a measured reason. | Gates renamed `*_download_mean_budget` and `*_upload_mean_budget`, and their detail text says "mean-rate gate, not a peak gate". Client status lines now carry cumulative bytes. `window_rates_kbit_s` reports the p95, p99 and peak of the 1 s window rates. These are reported, not gated. | **The v3 soak shows why a peak gate needs its own bound.** In moderate and harsh, 1 s download peaks **exceed the mean ceiling**: 23.95 against 23.39 kbit/s, and 24.84 against 24.08. Jitter bunching and duplication cause short bursts above the mean. A peak gate will be a later gate version with a separately derived bound (KI-20). |

**v3 soak** (real-time, 120 s, fresh `-g3` directories, 41 gates each):

| Run | Gates | RTT p50 / p99 | Mean in / out | 1 s window peak in / out | Recovery | Shed |
|---|---|---|---|---|---|---|
| `soak-2026-10-23-212948-lan-g3` | 41/41 | 10.5 / 12.1 ms | 21.6 / 15.1 | 21.9 / 15.4 | 21 ms | 0 |
| `soak-2026-10-23-213209-moderate-g3` | 41/41 | 104.3 / 120.5 ms | 21.1 / 22.3 | 23.9 / 23.7 | 154 ms | 9 |
| `soak-2026-10-23-213417-harsh-g3` | 41/41 | 204.6 / 237.5 ms | 19.3 / 29.4 | 24.8 / 29.6 | 1328 ms | 107 |

The host's `timeout_notices` counter is now logged: 1 per run, as expected for one stall. Experiment record: EXP-012. The v1 and v2 evidence is untouched.

---

## Feedback on review request #2 (real-time soak)

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #2 comment 6372084762](https://github.com/M0nsT3rG/ProjectMordor/pull/2#issuecomment-6372084762), posted from the repository owner's account. ChatGPT reviewed the source code only and did not run anything.
- **ChatGPT's verdict:** conditional GO for more Phase 2A lab work. It recommends hardening the analyzer and the timeout notice before calling the soak gates robust.

### Triage by Claude Code

Each finding was verified against the code first. Experiment record: EXP-011.

| Feedback ID | Assumptions verified? | Decision | Reason | Implemented in | Verification |
|---|---|---|---|---|---|
| **FB-2-01** (major, P0) | **Yes, demonstrated.** Analyzer v1 iterated `glob("client*.log")`, gated only clients it found, and compared joins with that found count; client2's stall gates were skipped when its log was absent. Run on a synthetic run where client2 never started (no log, `exit_codes.client2 = null`, host joins = 1), **v1 printed "SOAK PASSED"**. | **Accepted** | Gates must not depend on which files happen to exist. | Gate **version 2**: `run.json` carries `expected_clients`; new gates `manifest_lists_expected_clients`, `client_logs_match_manifest` (missing, renamed and extra logs all fail), `stalled_clients_in_manifest`, `exit_codes_cover_all_processes`, `host_joins_match_manifest`; every expected client is gated whether or not its log exists. Also, **evidence immutability**: the analyzer refuses to overwrite an existing `summary.json`, and `soak.ps1` writes every run to a fresh timestamped `…-g2` directory and never deletes. (The old script emptied the run directory and would have overwritten the failing evidence on the next run of the same profile.) | 8 new analyzer self-checks (missing, renamed and extra logs; missing manifest; joins against the manifest; no overwrite; budgets and coverage) plus the existing ones: **11/11**. v2 fails the synthetic broken run that v1 passed. |
| **FB-2-02** (major, P1) | **Yes, a real defect found.** The client checked endpoint, session and state, but not ordering. A `Disconnect{Timeout}` sent while the slot was in grace but delivered *after* a successful reconnect knocked the fresh connection back into Reconnecting. | **Accepted** for robustness. Authenticity: **deferred**; agreed it needs an authenticated transport. | The client records the sequence of the `ConnectAccept` that established the current connection and ignores any `Disconnect` with that sequence or older (`stale_notices`). This is a correctness guard, not a security control: a forged packet can still claim a newer sequence, so the prototype stays loopback and trusted-LAN only (KI-04). No nonce-only "security fix" was invented, as ChatGPT also advised. | `Client::handle` (`accept_seq_`), `ClientStats::stale_notices` | Fake-host tests written first: `delayed_timeout_notice_after_reconnect_is_ignored` and `replayed_and_duplicate_timeout_notices_have_no_extra_effect` **failed on the unfixed client** and pass now. `timeout_notice_from_other_session_or_endpoint_is_ignored` passes (session and endpoint were already checked). The real-host `timeout_notice_recovery_under_jitter_reconnects_once` passed before and after: it delivered no late notice (`stale_notices=0`), so it is labelled a smoke test, not proof. |
| **FB-2-03** (major) | **Yes.** `max_correction`, shed inputs and kbit/s were only reported. | **Accepted** for bandwidth, correction and coverage. Shed-input threshold: **deferred**. | The ceilings are **derived from the protocol design before any v2 run**, not fitted to results. **Download:** (87-byte snapshot × 30 Hz + 30-byte ack × 4 Hz) × (1 + duplicate) × 1.05. **Upload:** (119-byte full input batch × 30 Hz + 30-byte heartbeat × 4 Hz) × 1.05. **Correction:** speed × (RTT bound + 3-tick backlog), because reconciliation only replays unacknowledged inputs. **Coverage:** elapsed ≥ 95% of the duration, and RTT samples ≥ 50% of 4/s × duration. Correction percentiles (p50, p99) are now logged. A shed-input or queue-depth threshold has no design-derived bound yet and needs repeated baselines first. | `tools/soak/analyze.py` (v2), client summary (`correction_p50/p99`, `stale_notices`) | Self-check `test_budgets_follow_protocol_arithmetic` pins the arithmetic, and each new gate has a test proving it can fail. The v2 soak passed all 36 gates on all 3 profiles (results below). |
| **FB-2-04** (minor) | **Yes.** | **Accepted** (labelling). Tighter benchmark gate: **deferred.** | The 60 ms allowance is kept at the v1 value for comparability and is now labelled in code and gate output as a "synthetic loopback bound", not a LAN criterion. Gates are versioned, and old summaries keep `gate_version` absent (v1). A tighter lab benchmark needs repeated measurements and will be a new gate version. LAN gets its own baseline once it can be run. | `analyze.py` comments, gate detail text, `gate_version` | n/a |
| **FB-2-05** (minor) | **Yes.** There was one 5 s in-process stall in a 120 s run. | **Accepted, deferred** (P1 for the next soak iteration) | Agreed coverage gaps: host stalls, repeated and asymmetric outages, OS-level suspension, startup races, repeated reconnects within the grace period, longer randomized seeded runs. Out of scope for this fix round. | KNOWN_ISSUES KI-16 | — |
| **FB-2-06** (suggestion) | **Yes.** The analyzer comment still described the 15.6 ms sleep situation. | **Accepted** | The comment was updated to give the v1 rationale and the post-fix reality (about 4 ms). The analyzer now records each client's `high_resolution_timer` flag from its `loop_timer` log line. Before/after traces are preserved (EXP-010 directories untouched). | `analyze.py` | n/a |

### Answer 4: reliable event channel

- **ChatGPT's recommendation:** prefer **ENet or GameNetworkingSockets behind a small transport interface** over a custom reliability protocol.
- **Decision:** accepted as the direction, **pending a user decision**. Vendoring either library adds third-party code and licence review (ENet is MIT; GameNetworkingSockets is BSD-3). Project rules say the user decides on new dependencies.
- **What already exists:** before review #2 arrived I had started a minimal in-house `ReliableChannel` (protocol v2 `Event`/`EventAck`, 21 passing unit tests, including a 5-seed × 2000-event lossy/reordering property test). It is parked as an **unpushed local WIP commit** on `feature/reliable-events` and is not part of PR #2.
- **Proposal:** use it as (a) the transport-neutral interface and test oracle, and (b) one of the candidates measured against ENet and GameNetworkingSockets with the same event-order, deduplication, reconnect and fault-injection tests, as ChatGPT suggests.

### Soak results under gate v2

All runs are real-time, 120 s, 36 gates each.

| Run | Gates | RTT p50 / p99 | Max correction (budget) | In / out kbit/s (budgets) | Recovery after stall | Shed |
|---|---|---|---|---|---|---|
| `soak-2026-10-23-204617-lan-g2` | **passed** | 10.5 / 12.1 ms | 0 (0.84) | 21.6 / 13.7 (22.9 / 31.0) | 18 ms | 0 |
| `soak-2026-10-23-204825-moderate-g2` | **passed** | 103.9 / 121.6 ms | 0.33 (1.40) | 21.1 / 22.2 (23.4 / 31.0) | 161 ms | 8 |
| `soak-2026-10-23-205033-harsh-g2` | **passed** | 203.9 / 241.6 ms | 0.33 (2.00) | 19.2 / 29.4 (24.1 / 31.0) | 1567 ms | 110 |

**Caveats:**
- Harsh recovery varied between runs: 572 ms in EXP-010 and 1567 ms here, both under the 3000 ms gate. That variance is expected at 15% loss in both directions and is not yet characterised statistically.
- Harsh upload is close to its ceiling (29.4 of 31.0). That is expected: at RTT of 200 ms or more every input batch carries the full 8 redundant commands, so it sits at the design maximum.
- The host's `timeout_notices` counter was missing from the `host_stopped` log during these runs. It has been added since, so it appears only in future runs.

---

---

## Feedback on review request #1

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #1 comment 6371152419](https://github.com/M0nsT3rG/ProjectMordor/pull/1#issuecomment-6371152419), posted from the repository owner's account.
- **ChatGPT's verdict:** conditional GO for Phase 2A (standalone networking hardening); NO-GO for Steam game integration under the current constraints.
- **ChatGPT's stated limit:** it reviewed documents and source code only. It did not build or run anything.

### Summary of findings received

| ID | Area | Severity | Gist |
|---|---|---|---|
| FB-1-01 | protocol | major | `Host::step` drains the backlog by calling `apply_input(dt)` several times in one tick, so entity time runs ahead of world time |
| FB-1-02 | protocol | major | Unauthenticated token and endpoint identity; liveness was refreshed before checking the message's direction |
| FB-1-03 | tests | major | 38/38 passes are simulated-clock; real-time soak, process-pause and LAN tests are missing; a backlog test is the top priority |
| FB-1-04 | architecture | suggestion | Keep unreliable movement; add a separate reliable, ordered, idempotent event channel |
| FB-1-05 | evidence | minor | Make "CONFIRMED" explicitly scoped (`verification_scope`) |
| FB-1-06 | feasibility | minor | Keep G0 NO-GO explicitly conditional; seek jurisdiction-specific advice; GOG unverified |
| FB-1-07 | protocol | minor | The 30 s grace period exceeds the 15 s retry window; add adversarial reconnect tests |

### Triage by Claude Code

Every recommendation was checked against the code or documents before any action. Commit and test details are in [DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md) and EXP-009.

| Feedback ID | Assumptions verified? | Decision | Reason | Implemented in | Verification |
|---|---|---|---|---|---|
| **FB-1-01** (P0) | **Yes, with one correction.** `src/host/host.cpp` `Host::step` ran a `do { apply_input(dt) } while (pending > 2)` loop inside one tick: confirmed. The secondary claim that client replay "can diverge" is **not correct for the old code**. The client applies each input once and the host also applied each input exactly once, so final positions matched and reconciliation stayed consistent. The real defect is the **per-tick time budget**: within one tick an entity could move up to N × one timestep. That would break future collision, combat timing and AI, and it shows remote players a speed burst. | **Accepted** | A fixed tick must advance every entity by exactly one timestep. Policy (D-010): one input per tick; any backlog above 2 is shed oldest-first and acknowledged as consumed, so the client stops replaying it and reconciliation absorbs the difference. Rejected alternative: a "global catch-up budget" that advances world time, because it complicates authority for no benefit at this stage. Deferred: client-side input-rate adaptation. | `Host::step`, `HostStats::shed_inputs`, `docs/protocol.md`, D-010 | Tests written first and **failed on the old code**, then pass: `input_burst_never_exceeds_one_timestep_per_tick` (exactly 3 of 16 burst inputs simulated, 13 shed, one tick per step), `steady_inputs_are_never_shed`, `jitter_never_exceeds_tick_movement_budget` (80 ms jitter; worst per-tick displacement ≤ 1.0 timestep; prediction converges after stop) |
| **FB-1-02** | **Yes.** `Host::handle` set `last_recv_us` before the message-type check, and so did `Client::handle`. The token is a non-cryptographic 64-bit value that allows slot migration: confirmed, and already documented as "not authentication" (R-12). | **Partially accepted, partially deferred.** State-aware liveness: accepted and done. Authentication, integrity and rate limiting: deferred. | Liveness is a correctness bug, cheap to fix. Authenticated transport is a large design change that only matters before WAN or untrusted-LAN exposure, which project rules already forbid (D-006). It is tracked as a hard prerequisite for internet use. | `Host::handle`, `Client::handle` (direction filters); KNOWN_ISSUES KI-04 | `wrong_direction_messages_do_not_refresh_liveness` and `client_ignores_wrong_direction_messages_for_liveness` failed before the fix and pass after. `reconnect_token_migrates_slot_to_new_endpoint` pins down the documented (insecure) token behaviour. |
| **FB-1-03** | **Yes.** All suites use the simulated clock; real-time coverage is one E2E run; the LAN test is NOT_RUN. | **Partially accepted.** Backlog test: done. Labelling simulated versus real time: done. Real-time soak, process-pause and telemetry: deferred (P1). LAN: still blocked on firewall approval. | The P0 part is done. A soak with p95/p99 RTT and drift telemetry is a separate work item. | `tools/collab/report.py` (a Clock column plus a real-time note in TEST_RESULTS) | The report shows "simulated clock" per suite. Real-time E2E is kept separate. |
| **FB-1-04** | **Yes.** All 8 message types are unreliable (`docs/protocol.md`). | **Accepted as design direction, deferred** (Phase 2A P2) | Agreed: movement stays unreliable; consequential events need a reliable, ordered, idempotent plane. Nothing in the current scope sends such events yet, so building it now would be speculative. | D-010 (decision recorded); KNOWN_ISSUES KI-13 | n/a (design only) |
| **FB-1-05** | **Yes.** 33 CONFIRMED findings mixed our own measurements with statements about what sources contain. | **Accepted** | A cheap, high-value integrity improvement. Rule: `locally_measured` (F-ENV), `source_verified` (other CONFIRMED rows: we checked what the source says), `third_party_claim` (everything else). The validator now **rejects** CONFIRMED + `third_party_claim`. | All 62 findings (`verification_scope`), `tools/evidence/evidence.py`, `report.py` tables, CLAUDE.md conventions | Migration diff is whitespace plus 62 added lines; a semantic JSON comparison against HEAD showed 0 content changes. The validator reports 71 records, 0 errors, and a negative test confirms both new rules fire. Distribution: 29 `source_verified`, 25 `third_party_claim`, 8 `locally_measured`. |
| **FB-1-06** | **Yes.** The phase 1 report already says "not a technical verdict … unknown, not disproven", the matrix makes G0 conditional on D-007/D-008, and GOG is marked unverified. | **Accepted, no change needed** | The documents already reflect it. ChatGPT also confirms circumvention stays off the table. Its note "avoid inferring absence of a mechanism from absent static imports" is already scoped in F-ENV-006, which says absence from static imports proves nothing about runtime use. | — | Re-read `docs/phase1-report.md` §12 and `docs/feasibility-matrix.md` G0 |
| **FB-1-07** | **Yes.** 30 s grace versus a 15 s client window; `Host::on_connect` endpoint and token handling. | **Accepted** (documentation and tests). User-initiated retry: **deferred.** | The trade-off is intentional (capacity reservation) and now documented. The edge-case tests are added. Several suggested cases were already covered: old-nonce responses (`client_rejects_stale_snapshots` sends a wrong-nonce accept), sustained loss, and timeout/reconnect. | `docs/protocol.md` (capacity trade-off); KNOWN_ISSUES KI-11; tests | New tests: `reconnect_token_migrates_slot_to_new_endpoint`, `reconnect_after_grace_expiry_gets_new_identity`, `client_survives_host_restart_with_new_session`. One of them first failed because of a **test** bug: with only the uplink cut, the client correctly stays Connected until the host stops sending. That behaviour is recorded as KI-12. |

### Rejected recommendations

None outright. Two parts were narrowed:

- **FB-1-01:** the "global catch-up budget that advances world time" option was rejected in favour of shedding. See the reason above.
- **FB-1-01 secondary claim:** the claim that client replay can diverge was corrected; replay was consistent, and the actual defect was the per-tick time budget.

### Problems found while implementing (not raised by the review)

- **Exclusive log files.** `fopen_s` opens files exclusively. A concurrent reader of `host.log` made the host fail with "cannot open log file", and nothing could read a live log. Fixed: logs are now opened with `_fsopen(..., _SH_DENYWR)` via `cli::open_log`.
- **Stale wait in `test.ps1`.** The E2E wait loop polled the **previous run's** `host.log`, so the EXP-006 mitigation never really worked. Fixed: old logs are deleted first.
- **First run after a relink.** EXP-006 was re-tested and reproduced: the first run after a relink exchanged no packets. That **contradicts** the earlier "late start" hypothesis. The root cause is unknown. A warm-up run avoids it, and it is now part of `test.ps1` (KNOWN_ISSUES KI-08).

### Answers to ChatGPT's suggested priorities

| Priority | Item | Status |
|---|---|---|
| P0 | Backlog timestep correctness plus regression test | **Done** |
| P1 | Peer and message-state validation; reconnect tests | **Done** for liveness and edge cases. Authentication and rate limiting deferred. |
| P1 | Real-time soak and telemetry tests | Open |
| P2 | Reliable ordered idempotent events | Decision recorded (D-010); implementation open |
| P2 | Timer and RTT overhead, desync digests, LAN test (with approval) | Open |
