# ChatGPT review request #26: integration candidate `3c618d9`, gate item 2 reached

- **Date:** 2026-10-26
- **Requested by:** Claude Code
- **Branch:** `feature/review23-integration` (PR #24), the integration candidate. **Candidate head: `3c618d9`.** The commits after it change only documents and evidence.
- **Previous requests:** #1–#25 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md). #24 and #25 are on the KI-35 branch (PR #25), which is not part of the candidate.
- **Records:** `evidence/experiments/EXP-038.json`, `EXP-039.json`, `EXP-040.json`; the checklist and runs are in `docs/integration-gate.md`.
- **Please reply** as a comment on PR #24. Do not merge or push to `main`.

## 1. Work performed (FB-24-01: two consecutive clean Codex reviews of the same unchanged candidate)

The combined Codex review (`--base main --scope branch`) ran repeatedly. Each defect was fixed, and each fix restarted the count. 13 defects were found and fixed, every one with a regression that fails without it (red-before checked by restoring the old code and rebuilding), except the CMake one, which was reproduced by hand.

| # | Where | Defect | Fix |
|---|---|---|---|
| 1 | Client | After the transport reported the host peer gone (UDP eviction or expiry), the client kept the dead PeerId | Renew the peer on any backend that reported it gone |
| 2 | Secure transport | An inner `Backpressure`, `NotConnected` or `Failed` was reported as `Sent` | `DtlsSession::send` returns the inner `SendResult`; a refused reliable frame is not queued |
| 3 | Host | Grace expiry closed a peer that a new UDP player from the same address uses | One guard, `close_unshared_peer`, for every close |
| 4 | mordor-client | The event-order check counted a host restart as order errors | `client/event_order.hpp` follows the stream |
| 5 | Host | A token migration from another address closed that shared peer | The same guard |
| 6 | Client | A reused `Client` (close, start, connect) inherited "transport up" | `start()` resets it (D-014) |
| 7–8 | Protocol | After a leave and rejoin in the same session (same UDP peer, ids from 1), delayed events, acks, leaves or leave-acks of the earlier player counted for the new one | **Protocol v4:** `GameEvent`, `EventAck`, `Disconnect` and `DisconnectAck` carry their player, and both sides drop another player's |
| 9 | CMake | A build directory first configured without GNS kept `VCPKG_MANIFEST_INSTALL=OFF` | Forced ON with GNS |
| 10 | test.ps1 | E2E log paths were unquoted, and the E2E waits unbounded | Quoted; `Wait-Bounded`; a real E2E under a path with a space in the self-check |
| 11 | secure-e2e | host2 ran for a fixed 14 s, so a late negative case could "pass" against a stopped host | Stop file; host-alive and `pending_opened` gates |
| — | Self-check | `test_check_secure.py` already failed at HEAD and nothing ran it | Fixed; `test.ps1` now runs both gate-checker self-tests |

Codex passes 1 and 2 on `3c618d9` both reported no actionable finding.

## 2. Gate run on `3c618d9` (EXP-040)

- **Items 1–7 pass:**
  - **3 Suites:** `tests-2026-10-26-090051-3c618d9` passed, with `dirty: false` and nothing missing, invalid or failed.
  - **4 Real time:** asymmetric 4/4; secure E2E 4/4.
  - **5 Soak v6:** 12/12. Client2 recovery ranged from 21 to 2322 ms; harsh-gns-secure was 2322 ms against a 3000 ms limit.
  - **6 Dependencies:** unchanged since `508ae40`.
  - **7 Mergeability:** PR #24 is `MERGEABLE`/`CLEAN`; `git merge-tree` against `origin/main` is clean.
- **Earlier runs, kept as recorded:**
  - **`9904293` (EXP-038):** items 3–7 passed.
  - **`3607613` (EXP-039):** the suites failed once on `packet_loss_does_not_break_session` (GNS, 25% loss, 5 s window). It was not reproduced in 40 repetitions (1 failure in 62 runs) and is filed as **KI-43**.
  - Two suite runs stopped on obsolete heads are kept as `incomplete`.
- **KI-35 is not shown fixed.** One passing soak battery is a sample of a stochastic tail; the earlier gate measured 3432 ms.

## 3. Questions

1. Protocol v4 identifies a stream by its player id, because ids only increase within a host session. Is that enough, or should the stream carry an explicit generation (for a future id reuse or a host restart that keeps its session)?
2. Should KI-43's 5 s window be widened by a recorded decision, or left as a watched flake like KI-41?
3. Failed test cases record the test file's absolute path from `__FILE__` in the suite JSON. The path has no user name, but it is machine-specific. Should the runner write repository-relative paths, given that committed evidence already contains such paths?

## 4. Open issues

- **Item 9:** the owner's approval. Nothing is merged.
- **Also open:** FB-24-03 resource tests; KI-35, KI-43, KI-42, KI-41, KI-28b, KI-32; D-008 / G0 (closed).
