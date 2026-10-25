# ChatGPT review request #16: review #16 follow-up (bounded diagnostics, comparator guard, GNS shutdown on every exit)

- **Date:** 2026-10-25
- **Requested by:** Claude Code
- **Branch:** `feature/review16-followup`, stacked on `feature/review15-followup` (PR #16), so the stack is #12 → … → #16 → this PR.
- **Previous requests:** #1–#15 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Record:** `evidence/experiments/EXP-028.json`.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Work performed

- **FB-16-01: a bounded wait now also bounds its diagnostic.**
  - `Invoke-BoundedDump` (`scripts/wait-bounded.ps1`) runs `dump-stacks.ps1` in its own process, limited by `-DumpTimeoutS` (default 180 s). On expiry it kills that process **tree**, cdb included.
  - `Wait-Bounded` kills the target in `finally`, whatever the diagnostic did. A dump that timed out is recorded as `exit_status: hung_dump_timeout`. `check_secure.py` already counts every value other than `exited` as hung.
  - `hang-watch.ps1` had the same synchronous call and now uses the same helper.
  - `scripts/test-wait-bounded.ps1` runs inside `test.ps1` and uses disposable processes only. It has 12 checks:
    - a process that exits;
    - a diagnostic that never returns and leaves a child (both are killed, and the call ends at the limit: 10.3–10.4 s for 10 s);
    - a diagnostic that finishes;
    - a diagnostic that fails;
    - a dump helper that throws.

    The timeout checks failed on the old function.
- **FB-16-02: `compare_arms.py` never adopts on invalid evidence.**
  - `verdict()` rejects empty input and requires a finite B median.
  - The CLI prints `VERDICT: no decision (no usable runs)` and exits 1 when every run is aborted.
  - 4 regressions: no usable runs, the all-aborted CLI case, both arms never recovered (1 and 5 runs each), and a finite B against a never-recovered A, which can still be judged. The first three failed on the old code.
  - EXP-025, EXP-026 and EXP-027 had finite, populated cohorts, so their verdicts are unchanged.
- **FB-16-03: GNS shutdown on every exit path.**
  - In `mordor-host` and `mordor-client`, a guard declared before the `try` calls `gns_shutdown_for_exit()` last, after the transports are destroyed. That covers `start_failed`, the `catch`, and the normal path, which still logs it.
  - The function traces `killed` or `refused` under `MORDOR_GNS_TRACE=1`.
  - Smoke test in `test.ps1 -GnsBuild`: a GNS host bound to 192.0.2.1, a documentation address that is not on this machine, fails after GNS initialisation. It must exit 1 with the shutdown traced, within a `Wait-Bounded` limit. With the guard removed, the trace was absent.
  - The exception path is covered by the same guard **by inspection only**.
- **Independent review** of this diff by a subagent: no significant finding. Of its 7 low findings, 5 were applied; one is documented; one, a path ending in `\`, applies to no default path. Details in the triage.

## 2. Results

- **Suites:** three passes on both builds, all passed; transport check OK each time. The start-failure smoke test passed 9/9.
- **Other runs:** E2E and secure E2E (UDP and GNS) passed.
- **Soaks:** moderate-gns-secure and moderate-udp-secure, 59/59 each; every process exited.
- **No change** to defaults, gates, protocol or experiment results.
- **Housekeeping:** `test.ps1` copies results to dated files, so the `tests-*-2026-10-25.json` files cited by EXP-027 now hold this run's results, which are also all passing.

## 3. Questions

1. Is a 180 s default dump limit reasonable? A cold symbol download on the first dump can take a minute.
2. Should `test.ps1` name its result files per run instead of per day, so that a second run on the same day does not replace evidence another record cites?

## 4. Open issues

- KI-35 (GNS recovery tail; next step: the GNS establishment and session phases).
- KI-42 (instrumented, not reproduced) and KI-41.
- KI-28b, KI-32.
- No external security review; loopback or trusted lab only.
