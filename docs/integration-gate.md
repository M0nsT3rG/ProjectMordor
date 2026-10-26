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
| 2026-10-25 | `508ae40` (branch `feature/review23-integration`: PR #22 + #23 + gate fixes) | **Not yet passed.** Items 1, 3, 4, 6, 7, 8 pass. Item 5 passes except harsh-gns-secure (3432 ms, KI-35, accepted as open). Item 2: 5 Codex passes found and fixed 12 defects; pass 6 is pending (usage limit). Item 9: owner. | `evidence/experiments/EXP-035.json` |

| 2026-10-26 | `9904293` (the same branch, with 4 more fixes) | **Not yet passed.** Items 1 and 3–7 pass, and item 5 is 12/12. Item 2: Codex passes 6 and 7 on `2217085` found 3 P2 defects, and the combined pass on the fixed head found 1 more. All 4 are fixed with regressions. Two consecutive clean passes on the final head are pending. Item 9: owner. | `evidence/experiments/EXP-038.json` |

| 2026-10-26 | `3607613` (the same branch, after 6 more combined-pass fixes) | **Not yet passed.** Items 4 and 5 pass (soak 12/12). Item 3 failed once on `packet_loss_does_not_break_session` (GNS, 25% loss, 5 s connect window): not reproduced in 40 repetitions, filed as KI-43; a passing suite run on the final head is still needed. Item 2 pending (Codex usage limit). | `evidence/experiments/EXP-039.json` |

| 2026-10-26 | **`3c618d9`** (the same branch) | **Items 1-7 pass.** Item 2: two consecutive clean Codex passes on this unchanged head. Suites passed clean; asymmetric 4/4; secure E2E 4/4; soak 12/12; merges cleanly. Item 9 (owner approval) is open; nothing is merged. | `evidence/experiments/EXP-040.json` |

### Item details for `9904293`

1. **Candidate:** `9904293`. No uncommitted source. Commits after it change only documents and evidence.
2. **Combined review:** the four fixes, each with a regression that fails without it:
   - the client renews a transport peer that the backend reported gone (UDP eviction or expiry);
   - the secure decorator returns the inner backend's own `SendResult` (refusals were reported as `Sent`);
   - grace expiry never closes a peer that a connected player uses (a new UDP player from the same address);
   - `mordor-client`'s event-order check follows the event stream (a host restart counted false order errors).

   The combined pass on `c011a0c` found a fifth defect of the same kind: a token migration from another address closed a peer that a new player shares. It is fixed with one guard for every close the host makes, plus a regression. Items 3–5 are therefore rerun on the new head (see the next row).

   **The two consecutive clean passes are pending.**
3. **Suites:** `tests-2026-10-26-010843-9904293` passed, with `dirty: false` and nothing missing, invalid or failed. A run on `951fd77` was stopped on purpose when the combined pass found the fourth defect; it is kept and recorded as `incomplete`.
4. **Real time:** asymmetric 4/4, secure E2E 4/4.
5. **Soak v6:** 12/12. harsh-gns-secure recovered in 1853 ms. That is one sample of a stochastic tail, so KI-35 is **not** shown fixed: the previous run measured 3432 ms.
6. **Dependencies:** `vcpkg.json`, `CMakeLists.txt` and `docs/third-party.md` are unchanged since `508ae40`.
7. **Mergeability:** `git merge-tree` against `origin/main` is clean.
8. **Scope:** unchanged (see below).
9. **Owner approval:** not given, and nothing is merged.

### Item details for `508ae40`

1. **Candidate:** `508ae4088b2540cb40b9b57a7035419c97e53d83`; no uncommitted source; contains every closed PR's head and #23.
2. **Combined review:** 5 Codex passes on `--base main`. Each pass found real defects in code that per-PR reviews had accepted, which confirms why this gate exists. All 12 are fixed, each with a regression that fails without its fix. **Pass 6 pending.**
3. **Suites:** `tests-2026-10-25-201116-508ae40`: passed, `dirty: false`, nothing missing, invalid or failed.
4. **Real time:** asymmetric 4/4, secure E2E 4/4.
5. **Soak v6:** 11/12. harsh-gns-secure failed `client2_recovery_time` only (KI-35).
6. **Dependencies:** GNS 1.6.1, OpenSSL 3.6.5, protobuf 7.36.2, abseil 20260107.1, as pinned. Licences BSD-3 (GNS, protobuf), MIT (utf8-range, corrected), Apache-2.0 (OpenSSL, abseil); all permit this use; notices listed for any redistribution.
7. **Mergeability:** PR #22 `MERGEABLE`/`CLEAN`; the candidate merges into `main` without conflicts (`git merge-tree`).
8. **Scope:** see "What a merge would and would not mean" and "Still open".
9. **Owner approval:** not given; nothing merged.
