# Integration gate: merging the consolidated PR into `main`

Review #23 (FB-23-02): PR #22 now targets `main` with the whole stacked history (PRs #1–#21 were closed without
merging on 2026-10-25; their heads are all contained in #22). The per-PR reviews are history; they are **not**
an integration approval. A merge needs every item below, recorded on the **exact candidate head**, and then the
owner's explicit approval. Nothing is merged automatically.

The candidate head must include every stacked follow-up (#23 and later): the integration branch is the newest one.

## Checklist

| # | Item | How | Pass when |
|---|---|---|---|
| 1 | Candidate head identified | `git rev-parse` of the newest stacked branch; record it | One commit, clean (no uncommitted source) |
| 2 | Combined diff reviewed against `main` | Codex review `--base main --scope branch`, plus the owner's own look at the summary | No unresolved P1/P2 finding |
| 3 | Test suites on the candidate | `scripts/test.ps1 -GnsBuild -Repeat 2 -E2E` | `run.json` `result: passed`, `dirty: false`, nothing missing, invalid or failed |
| 4 | Real-time end-to-end | `scripts/asymmetric.ps1` × 4 (2 scenarios × 2 transports), `scripts/secure-e2e.ps1` × 2 per transport | All pass |
| 5 | Soak gate v6 | `scripts/soak.ps1`, the 12 scenarios (lan, moderate, harsh × udp, gns × plain, secure) | All gates pass, **or** a failure is a known open issue named in item 8 (KI-35: harsh secure recovery tail) |
| 6 | Dependencies and licences | `vcpkg.json` baseline; installed versions; `docs/third-party.md` | Versions match the pinned baseline; every licence permits our use; redistribution notices listed |
| 7 | Mergeability | `gh pr view <n> --json mergeable,mergeStateStatus` | `MERGEABLE` / `CLEAN` against `main` |
| 8 | Scope: accepted versus still open | This document, filled in for the candidate | Every open issue is named as **not** fixed by the merge |
| 9 | Owner approval | In chat, naming the candidate commit | Explicit; ChatGPT feedback is advice, never approval |

## What a merge would and would not mean

- **Would:** the standalone networking prototype (UDP and GNS transports, DTLS security, sessions, reliable events, the
  soak and evidence tooling) becomes the integrated baseline on `main`.
- **Would not:** any game integration. Gate G0 is closed (D-007 recorded, **D-008 pending**); M1–M10 are NO-GO.
  Release, LAN or internet use beyond trusted lab networks, and untrusted networks are out of scope (no external security review).

## Still open at the time of writing (not fixed by a merge)

- **KI-35:** harsh secure reconnect recovery tail, worst on GNS.
- **KI-42:** a rare missed GNS disconnect callback (instrumented, not reproduced).
- **KI-41:** a rare smoke-test flake under 60% loss (explicit contract).
- **KI-28b:** reliable events are not durable across a host restart. **KI-32:** cumulative acknowledgements only.
- No external security review; G0 / D-008.

## Recorded runs of this gate

| Date | Candidate | Result | Record |
|---|---|---|---|
| 2026-10-25 | see EXP-035 | see EXP-035 | `evidence/experiments/EXP-035.json` |
