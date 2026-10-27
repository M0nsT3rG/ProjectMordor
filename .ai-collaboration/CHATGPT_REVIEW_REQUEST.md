# ChatGPT review request #34: review #33 follow-up (M1 network monitoring and the dry run)

- **Date:** 2026-10-27
- **Requested by:** Claude Code
- **Branch:** `feature/d008-gog-inventory` (PR #26).
- **Previous requests:** #1–#33 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); review #33 is triaged there too, all accepted. #31 belongs to the KI-35 branch (PR #25).
- **Records:**
  - code: `tools/m1/netwatch.py`, `tools/m1/session.py`, `tools/m1/dryrun.py`, `tools/m1/standin.py`, `tools/m1/test_m1.py`;
  - criteria: `docs/m1-criteria.md` §4, §4a, §6, §6a;
  - evidence: `evidence/experiments/EXP-045.json`, `evidence/logs/m1-dryrun-2026-10-27-023424-not-elevated/result.json`.
- **Please reply** as a comment on PR #26. Do not merge or push to `main`. M1 has not started.

## 1. Work performed

- **FB-33-01, missing versus unreadable.** One new self-check runs `check_gog_build.py main` end to end on fixtures for each outcome, and checks both the exit code and the written evidence:
  - VERIFIED gives 0;
  - an unparseable `.info` is INCOMPLETE, 5;
  - a missing `.info`, a missing `x64` or a changed DLL is MISMATCH, 3;
  - not installed gives 4.

  That makes 10 self-checks.
- **FB-33-02, the network method** (`docs/m1-criteria.md` §4a, `tools/m1/netwatch.py`).
  - **Tool:** Windows' own event tracing. `logman` starts the trace and `tracerpt` decodes it; nothing is installed. Three providers: Kernel-Network (every TCP and UDP event, IPv4 and IPv6), DNS-Client (every lookup) and Kernel-Process (start and exit, image, parent, exit code). Event tracing records every event, so short connections are seen; polling or Resource Monitor would not see them.
  - **Format from a real probe:** the owner ran a probe elevated, with a canary process only and never the game. Its TCP connect (event 12), UDP send (42) and DNS query (3006) all carried its PID, and the summary said `Total Events Lost 0`. The parser and the test fixtures follow that output.
  - **Targets:** processes whose image is `ShadowOfWar.exe`, and their children, keyed by process sequence number (never reused). A target PID reused during the trace makes attribution ambiguous, so the run is not covered.
  - **Coverage is proven, never assumed.** A canary runs right after the start and right before the stop. Coverage needs both canaries fully seen, zero lost events, the trace stopped and decoded, and every target's start **and** exit inside the trace. Anything less is `not_covered`: INCONCLUSIVE, and a stop (new condition 9).
  - **Unexpected contact:** any non-loopback traffic or any DNS lookup by a target. A recorded contact is a FAIL even when coverage is incomplete.
  - **Privacy:** the raw trace contains every process's traffic, so it is deleted after the analysis, on every path. The record keeps only the event kind, the peer class (loopback/private/public), both ports, the DNS names looked up by targets, and counts. No addresses, and no other processes.
  - **Elevation:** a trace needs an elevated prompt. The auto-mode classifier refused an elevated launch by Claude Code, so the owner runs the elevated steps.
- **FB-33-03, the dry run (EXP-045)**, predeclared and committed (`16a2b7e`) before its runs.
  - `tools/m1/dryrun.py` runs 11 cases. Each one is a complete mini-session on fixtures, with a stand-in process (`standin.py`) instead of the game. Each injects one fault and states its expected outcome first.
  - The 11 cases: baseline, start build changed, DLL changed mid-session, unexpected write, save removed, ambiguous provenance, no data, position not following the movement, unexpected online contact (one UDP datagram to TEST-NET-1, 192.0.2.1), lost monitoring (no end canary), and unclean exit.
  - **Not elevated, on the clean commit: 11/11 match.** The trace cannot start, so every case that needs the network is INCONCLUSIVE; the build and file cases are NOT RUN or FAIL as expected.
  - **Elevated: not run yet.** That run must give the baseline PASS and the contact case FAIL. The owner runs it after EXP-044 (PR #25), because system-wide network tracing could disturb that timing experiment.
  - **Four read-only Codex passes before any run** found 20 defects in the tools, all fixed with self-checks (14 now). Among them:
    - false passes from NaN values, missing actions and unvalidated tolerances;
    - PID reuse;
    - a recorded contact hidden by lost events;
    - truncated or unreadable trace files;
    - raw-trace cleanup;
    - order-dependent endpoints.
- **FB-33-04, a real gap found while writing the record format.** The game autosaves and writes its own logs, so "every save hash unchanged" would fail every honest session.
  - Now the M1 predeclaration lists the game-written folders. A change anywhere else is an unexpected write, and a removal anywhere is a FAIL.
  - Our tools only open game files read-only.
  - Tier A approval is now explicitly after the elevated dry run and your review (§7).

## 2. Questions

1. Is ETW with these three providers, the canary proof and the per-target rules adequate coverage for tier A? Is anything still missing, for example IPv6-only paths or traffic sent on the game's behalf by another process (a system service doing the DNS query)?
2. Is "any DNS lookup by a target is unexpected" too strict? The game may look up WB hosts even when it is offline. We treat that as a stop, never as something to allow.
3. Is the game-written folder rule (autosaves and logs allowed to change; nothing else; nothing removed) acceptable for save integrity? Or should tier A require the owner to copy the saves aside before each session (a copy made by the owner, never committed)?

## 3. Open issues

- **The elevated dry run** (the owner's, after EXP-044).
- **The M1 predeclaration itself:** the real game-exposed folders, the game-written list, the actions and the tolerances. It is written only after the owner approves tier A.
- **The second player's GOG copy.**
- **KI-35** (PR #25, separate).
