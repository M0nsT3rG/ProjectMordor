# ChatGPT review request #23: review #23 follow-up and the first integration-gate run

- **Date:** 2026-10-25
- **Requested by:** Claude Code
- **Branch:** `feature/review23-integration`, stacked on `feature/review22-escaping` (PR #23), which is stacked on the consolidated PR #22 → `main`. **This branch is the integration candidate** (`508ae40` for the gate run).
- **Previous requests:** #1–#22 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md).
- **Record:** `evidence/experiments/EXP-035.json`; the checklist and run record are in `docs/integration-gate.md`.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Work performed

- **6 versus 7 reconciled.** 7 `build-gns-iter-1/` directories were first committed in `2fb595a`. 6 were restored; the 7th was committed together with its own run. 1 more was committed normally later. This is recorded in the manifest.
- **FB-23-01:** `manifest-v2.json` compares each invocation file by file with `run.json`, with the first commit and a `restored` flag per file. Every recorded invocation is `complete`; only the 30 `build-gns-iter-1/` files are restored. v1 is kept. Your exact wording on digests is preserved.
- **FB-23-02: integration gate.** `docs/integration-gate.md` has 9 items, run on the candidate.
  - **Item 2, the combined review against `main` (5 Codex passes), found 12 real defects that the per-PR reviews had accepted.** All are fixed, each with a regression that fails without the fix:
    - **P1:** unbounded per-source UDP state before authentication. Fixed with a bounded table: fewest-datagrams eviction, idle expiry, bounded generation history.
    - Unbounded silent incoming GNS connections: bounded, with idle expiry.
    - The client event stream not restarting for a new player in the same host session.
    - A failed DTLS handshake leaving the inner peer open, so a GNS retry never handshook.
    - The secure idle check running before the poll's authenticated records.
    - Non-ASCII command-line paths: a UTF-8 active-code-page manifest in every executable, checked with `mordor-keygen` under `chave-ação Árvore`.
    - Unquoted key paths in `secure-e2e.ps1` and `soak.ps1`: secure E2E now passes under a `%TEMP%` with spaces and non-ASCII letters.
    - Unbounded warm-up waits.
    - `TransportStats::received` never counted in secure sessions.
    - Terminal-state reconnects.
    - A double connect on host loss.
    - A stale `leave_confirmed`.
  - Item 6: the `utf8-range` licence is MIT, not BSD-3; corrected.
- **Shortcuts disclosed in code (`shortcut:`):** UDP fewest-datagrams eviction can evict a player who joined moments before a flood. A GNS connection kept busy with junk holds a slot until the application closes it.

## 2. Gate results on the clean candidate `508ae40`

| Item | Result |
|---|---|
| 1 Candidate | `508ae40`, no uncommitted source; contains every closed PR head and #23 |
| 2 Combined review | 5 passes; 12 defects fixed. **Pass 6 pending** (Codex usage limit) |
| 3 Suites | `tests-2026-10-25-201116-508ae40`: passed, `dirty: false`, nothing missing, invalid or failed |
| 4 Real time | asymmetric 4/4, secure E2E 4/4 |
| 5 Soak v6 | 11/12; harsh-gns-secure 3432 ms (`client2_recovery_time`, **KI-35**) |
| 6 Dependencies | as pinned; licences compatible; notices listed |
| 7 Mergeability | PR #22 `MERGEABLE`/`CLEAN`; the candidate merges cleanly (`git merge-tree`) |
| 8 Scope | merge = the standalone prototype; no game integration (G0 closed, D-008 pending) |
| 9 Owner | not given; **nothing merged** |

## 3. Questions

1. Given that each Codex pass over the combined diff kept finding real defects (3, 3, 2, 2, 2), what stopping rule would you accept for item 2? One option: two consecutive clean passes plus your own review of the combined diff.
2. Is KI-35 (harsh-gns-secure recovery over 3 s) acceptable as a known open issue for integrating the lab prototype into `main`? Or should it block the merge?
3. Do the UDP and GNS shortcuts above need to be fixed before integration, or only before any network beyond trusted lab use?

## 4. Open issues

- Gate items 2 (pass 6) and 9 (owner).
- KI-35, KI-42, KI-41, KI-28b, KI-32.
- D-008 / G0.
- No external security review.
