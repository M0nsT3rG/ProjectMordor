# Development log

Newest entries first. Commits are short hashes on the branch named in each entry.

## 2026-10-23: ChatGPT collaboration bridge

- **Commit:** see `git log` on `feature/ai-collaboration-bridge`. This entry is written before the commit exists.
- **Changes:**
  - Added the `.ai-collaboration/` bridge files.
  - Added `tools/collab/report.py`, a local status and review-summary generator with no network access.
  - Added the collaboration rules to `CLAUDE.md`.
- **Tests:** ran `report.py` against the repository and checked its output against `git` and `evidence/logs`. The prototype suites were not re-run, because no C++ code changed.
- **Results:** see [TEST_RESULTS.md](TEST_RESULTS.md).
- **Problems:**
  - There is no GitHub remote yet.
  - The `gh` CLI is not installed.
  - Creating the repository and pushing need user authorisation.
- **Remaining:** create the private `ProjectMordor` repository, push, and open the PR, once authorised.

## 2026-10-23: Phase 1 (commit `7766667`, `main`)

- **Changes:**
  - Read-only game discovery and PE inventory.
  - Evidence store (62 findings, 8 experiments) with a validator.
  - Prior-art review.
  - Architecture, subsystem notes, feasibility matrix, risk register and decision docs.
  - Standalone UDP networking prototype with 38 tests.
  - Phase 1 report.
- **Tests:**
  - `ctest` (unit 15, integration 13, network 10), 5 or more consecutive passing runs.
  - `scripts/test.ps1 -E2E`, which runs one host and two clients at 40 ms latency, 20 ms jitter and 10% loss.
- **Results:** all passed. The LAN test is NOT_RUN (EXP-007).
- **Problems encountered and fixed:**
  - A `const char*` log field printed `true`.
  - A lost Disconnect packet.
  - A first-launch start delay flaked the E2E run (EXP-006).
  - Seven review defects (EXP-008): 16-bit id wrap, input-backlog ratchet, liveness not refreshed by retransmits, grace-slot hijack, stale ConnectAccept, and two weak tests.
- **Abandoned:** a static probe of the executable's sections (EXP-003), because it conflicts with the anti-circumvention rule.
- **Remaining:** see [CURRENT_TASK.md](CURRENT_TASK.md) and `docs/phase1-report.md` §10.
