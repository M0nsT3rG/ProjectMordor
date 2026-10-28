# Development log

Newest entries first. Commits are short hashes on the branch named in each entry.

## 2026-10-28: The §8 amendment for KI-45 (`feature/d008-gog-inventory`, PR #26)

- **Request:** after EXP-048 session 1, the owner asked for the fixes to be prepared for a new approval.
- **The amendment** (§4a, §6a, §7 item 3, §8; marked "amended"):
  - **Trace buffers:** `netwatch.start` passes `-bs 256 -nb 64 512`, at most 128 MB. The trace state, the analysis and `session.json` keep the buffers used and `events_total`.
  - **Windows-1252 fallback:** a file with no BOM that is not valid UTF-8 is read as Windows-1252 and listed so. In such a read, a non-ASCII line containing `pos` or `loc` is a candidate, so it cannot pass as no data. A file whose encoding changes during the session is unreadable evidence.
  - **`user/gpu.nfo`:** while it does not decode and has a NUL, it is "known binary: hashed, not searched".
  - **The limit of NO DATA:** only ASCII labels are recognised; §8 states this.
  - **New records:**
    - EXP-050 measures the buffers: `coverage.py` gains `flood_with_session_buffers`, re-run elevated together with `dryrun.py`.
    - EXP-051 is the new tier A series, with sessions numbered from 1.

    Both are NOT_RUN. EXP-048 stays PARTIAL.
- **Codex passes 9 and 10** found 3 and 4 defects, all fixed or, for one, documented:
  - **Pass 9:**
    - the gpu.nfo exception skipped UTF-16 text;
    - mixed-encoding lines lost their label;
    - the flood check gave a MATCH when the stand-in stopped early.
  - **Pass 10** found the pass 9 projection unsafe:
    - a `1½` read as 1;
    - an encoding switch made old lines look new;
    - a widened gap hid a label;
    - a soft hyphen inside a UTF-8 label. This one is not changed in code: it is the ASCII-label limit, now stated in §8.

  The projection was replaced by the candidate rule and the encoding-change rule.
- **Tests:** 33 self-checks, 1 skipped. The new tests fail against the earlier code.
- **Codex pass 11** found 3 more, all fixed: gpu.nfo turning from binary to text made old lines look new (now any change of read mode is unreadable evidence); the NO DATA wording; EXP-051's expected reasons, which missed the two zero counts. **Pass 12** found the amendment consistent, apart from one wording point: the gap also excludes `+`, `-` and `.`.
- **Next:** EXP-050 and the dry run elevated (the owner), review, and the owner's approval. Until then `tier_a.py` must not be run.

## 2026-10-28: EXP-048 session 1 (`feature/d008-gog-inventory`, PR #26)

- **Run:** the owner ran `tier_a.py --session 1` elevated on the pinned GOG set, with the tool at `757ac6a` (clean). Evidence: `evidence/logs/m1-tier-a-2026-10-28-113203-s1/session.json`.
- **Outcome: INCONCLUSIVE, `no_data_established` false.** EXP-048 is PARTIAL.
- **Gates held:**
  - the build check was 0 at both ends;
  - nothing was removed;
  - only GAME_WRITTEN files changed (three saves, plus a new slot under an existing id);
  - the exit code was 0, inside the trace, through the menu;
  - no warning and no crash were reported;
  - both canaries were seen;
  - no contact or DNS lookup was attributed to the game;
  - the raw trace was deleted.
- **No position** in the 6 permitted files that were read.
- **Why NO DATA is not established (KI-45):**
  - 1203 lost ETW events: logman's default buffers were never sized for a real session.
  - `install/EULA.txt` is Windows-1252 text.
  - `user/gpu.nfo` is binary (134 of 208 bytes are NUL).

  The two file diagnoses come from byte statistics only.
- **Next:** the series is stopped and back with the owner. The proposed fixes (bigger buffers, measured with `coverage.py`; an encoding decision for the two files) change §8 and the tools, so they need review and a new approval. Nothing is changed yet. Review request #39.

## 2026-10-27: The owner approves M1 tier A (`feature/d008-gog-inventory`, PR #26)

- **Decision:** the owner approved `docs/m1-criteria.md` §8 in chat on 2026-10-27, as of commit `6f2ed04` (code `e171fe0`). The approval follows reviews #36 and #37 and Codex passes 7 and 8.
- **What it allows:** EXP-048 session 1, which the owner runs elevated after the KI-35 experiment on PR #25 has finished.
- **What it does not allow:** tier B. Any later change to §8 or to the session tools needs a new approval.
- **EXP-048:** still NOT_RUN.

## 2026-10-27: Codex pass 8 on EXP-048 (`feature/d008-gog-inventory`, PR #26)

- **Run:** the owner asked for it, to check the pass 7 fixes. Read-only, on `25edf8a`.
- **Found 1 defect, reproduced and fixed with a regression:** a save id equal to the machine name (the Steam-era save) was left out of id replacement, and names are replaced only as whole tokens. So `backupDESKTOP-T.cfg` kept the name in the evidence. A name that is also a save id is now replaced anywhere, like an id.
- **Tests:** 28 self-checks, 1 skipped. The extended test fails against `25edf8a`.

## 2026-10-27: Codex pass 7 on EXP-048 (`feature/d008-gog-inventory`, PR #26)

- **Run:** the owner asked for it after the usage limit reset. Read-only, on `e1683ec`.
- **Found 4 defects. All 4 reproduced and fixed, with regressions:**
  1. A UTF-16 permitted file was decoded as UTF-8. Its labels became unreadable and the file counted as scanned, so a session could establish NO DATA falsely. Now only UTF-8 or UTF-16 with a BOM is read; anything else is unreadable evidence.
  2. `SAVE_ID` also matched `<id>_ShadowOfWar.sav.bak`, so a backup could vouch for a new id. Ids now come only from whole save names (`SAVE_NAME`).
  3. A known id inside a longer name (`backup4e57de68.cfg`) was not redacted. Ids of 4 characters or more are now replaced anywhere.
  4. A session killed with its console left its trace running into its own temporary folder, which the next session never looked at. Before each session the tool now stops the trace and deletes earlier sessions' folders, and refuses to start if a raw file cannot be deleted.
- **Tests:** 28 self-checks, 1 skipped. The new and extended tests fail against `e1683ec`.
- **Open:** the owner's approval of §8; session 1 after the KI-35 run.

## 2026-10-27: Review #37 triage (`feature/d008-gog-inventory`, PR #26)

- **Input:** [review #37](https://github.com/M0nsT3rG/ProjectMordor/pull/26). §8 can go to the owner, conditionally. Comparing save ids with the before-snapshot is accepted. No code changes were requested.
- **Done:** the self-checks were run verbosely on the exact head `cf03239`, with a clean tree, and kept as evidence: `evidence/logs/m1-selfchecks-2026-10-27-143920-cf03239/`, 27 (1 skipped) and 10, all passing, the fixture-only rehearsal included.
- **Open before session 1:** Codex pass 7 (deferred by the owner), the owner's explicit approval of §8, the end of the KI-35 run.

## 2026-10-27: Review #36 follow-up: EXP-048 changes before approval (`feature/d008-gog-inventory`, PR #26)

- **Input:** [review #36](https://github.com/M0nsT3rG/ProjectMordor/pull/26). Conditional: make the outcome tests, the privacy checks and a fixture-only rehearsal before the owner approves §8. All six items accepted (triage in CHATGPT_FEEDBACK.md).
- **Fixed:**
  - A save under a new id is now an unexpected write.
  - The install's small text files are hashed.
  - Save ids are redacted wherever they appear, and path redaction is case-insensitive and catches escaped paths.
  - A failed trace analysis is recorded, not a crash.
  - A failed evidence write reports what it left.
  - `no_data_established` tells a valid NO DATA session apart from failed monitoring.
- **Tests:** 27 self-checks, 1 skipped. The 4 new ones fail against the previous code.
- **Open:** Codex pass 7 (the owner deferred it), the owner's approval of §8, then session 1 after the KI-35 run.

## 2026-10-27: Review #35 follow-up: the M1 tier A predeclaration (`feature/d008-gog-inventory`, PR #26)

- **Input:** review #35 and its follow-up on [PR #26](https://github.com/M0nsT3rG/ProjectMordor/pull/26). The prerequisites are met, so the final tier A predeclaration can be prepared for the owner's approval. The owner asked for the instructions to be carried out and will run the test.
- **F-ENV-014:** a metadata-only listing of the game's folders, approved by the owner. The game writes no log file.
  - The per-user folder holds the saves, render.cfg, gpu.nfo and a CloudStorage folder from the Steam era.
  - Save names carry the machine name and an 8-hex-digit id, which are never recorded.
  - Documents/WB Games is Shadow of Mordor's.
- **FB-35-04:** Ctrl+C or an error in the dry run or the coverage run left the system-wide trace running. `netwatch.abort` now stops it and deletes the raw files.
- **EXP-048 (NOT_RUN) and `docs/m1-criteria.md` §8:**
  - exact folders, permitted reads and game-written files (`label/<glob>` patterns in `session.game_written`);
  - actions guided by beeps, tolerances, backup and gates.
  - The hypothesis is Tier A INCONCLUSIVE / NO DATA.
- **Tool:** `tools/m1/tier_a.py`, which the owner runs elevated. It refuses to start without elevation, with Galaxy, Steam or the game open, or without a confirmed save backup. Its evidence holds no paths, names or save ids. 20 self-checks.
- **Open:** the owner's approval of §8, then session 1.

## 2026-10-27: Elevated runs of EXP-045 and EXP-046 (`feature/d008-gog-inventory`, PR #26)

- **Who:** the owner ran both tools from an elevated PowerShell on `b500439`, after EXP-044 and its test battery on the KI-35 branch had finished. The game was never touched.
- **EXP-045:** PASSED, 11/11, each for its predeclared reason.
  - The baseline was PASS with a clean network verdict.
  - The online-contact case was FAIL on "unexpected online contact".
  - Lost monitoring was INCONCLUSIVE.
- **EXP-046:** PASSED, 7/7 MATCH, with no NOT_APPLICABLE or NOT_DEMONSTRATED. Each mechanism was attributed with the claimed protocol and IP family:
  - IPv6;
  - the TCP attempt that never connected;
  - DNS only;
  - the 50 brief flows;
  - the child's send.

  The forced-loss case lost 164,760 events and gave not_covered.
- **Raw traces:** none was left in any case.
- **Open:**
  - ChatGPT's answer to #35, with these results added;
  - the owner's decision on tier A (M1 not started).

## 2026-10-27: Review #34 follow-up: ETW coverage experiment and raw-trace lifecycle (`feature/d008-gog-inventory`, PR #26)

- **Input:** [PR #26 review #34](https://github.com/M0nsT3rG/ProjectMordor/pull/26); the owner asked for all of it.
- **FB-34-01:** EXP-046 was predeclared (`60ae022`) before its runs. `tools/m1/coverage.py` covers 7 mechanisms:
  - IPv6 loopback;
  - a TCP attempt that never connects;
  - UDP to 2001:db8::1;
  - a DNS lookup only;
  - 50 short flows;
  - a child process's send;
  - forced event loss.

  A MATCH needs the claimed protocol (from the ETW task name), the IP family and proven coverage. Contacts now carry the protocol, the family and whether a child sent them. **Not elevated: 7/7 not covered**, as expected.
- **FB-34-02/03:** `docs/m1-criteria.md` says what a clean record does not prove, and asks for the owner's offline save copy.
- **FB-34-04:**
  - A kept or undeletable raw trace is a FAIL (stop condition 10).
  - A trace never starts beside old raw files.
  - New command: `netwatch.py cleanup`.
- **EXP-045:** each case now also needs its predeclared reason. Re-run not elevated on `60ae022`: 11/11.
- **Codex:** three passes. The first found 5 defects and the second 4, all fixed (17 self-checks); the third was clean.
- **Found on the way:** Python's `ipaddress` classes the documentation ranges as `is_private`. That does not matter, because any non-loopback peer counts.
- **Open:** the owner's elevated runs of EXP-045 and EXP-046 after EXP-044; ChatGPT's answer to #35.

## 2026-10-27: Review #33 follow-up: M1 network monitoring and the dry run (`feature/d008-gog-inventory`, PR #26)

- **Input:** [PR #26 review #33](https://github.com/M0nsT3rG/ProjectMordor/pull/26); the owner asked for all of it, without starting M1.
- **FB-33-01:** `check_gog_build.py` `main` is run end to end for each outcome, with its exit code and evidence file checked (10 self-checks).
- **FB-33-02:**
  - `tools/m1/netwatch.py` uses Windows' own event tracing (`logman`/`tracerpt`).
  - The event format comes from a probe that the owner ran elevated: a canary process's TCP connect, UDP send and DNS query, each with its PID.
  - Coverage is proven by canaries, zero lost events, and the target's start and exit inside the window. Processes are keyed by sequence number.
  - The raw trace is deleted afterwards; the record holds no addresses.
- **FB-33-03:** `tools/m1/session.py` holds the outcome rules; `tools/m1/dryrun.py` runs EXP-045, 11 fault-injection cases on fixtures.
  - Four Codex passes before the runs found 20 defects; all are fixed (14 self-checks).
  - **Not elevated: 11/11 match.** The elevated run is the owner's, after EXP-044.
- **FB-33-04:** found while writing the record format: the game autosaves and writes logs. The predeclaration now lists the game-written folders. A change elsewhere, or any removal, is a FAIL.
- **Elevation:** the auto-mode classifier refused an elevated launch by Claude Code, so the owner runs elevated steps.
- **Open:** the elevated dry run; ChatGPT's answer to #34; then the owner's decision on tier A; the second player's copy.

## 2026-10-27: Review #32 follow-up: a fail-closed pin check and M1 outcomes (`feature/d008-gog-inventory`, PR #26)

- **Input:** [PR #26 review #32](https://github.com/M0nsT3rG/ProjectMordor/pull/26); the owner asked for all of it, without starting M1.
- **FB-32-01:** already done (F-ENV-013, reported after request #32). Both fingerprints stay recorded.
- **FB-32-02:** `check_gog_build.py` now fails closed.
  - MISMATCH (exit 3) and the new INCOMPLETE (exit 5): an unreadable FileVersion, malformed `.info` files and unreadable files are never verified.
  - It pins the SHA-256 of all 7 files in `x64` as an exact list (hashed read-only on this machine), so a changed, missing or extra DLL fails.
  - A Codex pass found four checker defects and two contradictory outcome rules; all fixed.
  - 9 self-checks; `inventory.ps1` throws on exit 5 and passes on this machine.
- **FB-32-03/04:** `docs/m1-criteria.md` gains Tier A INCONCLUSIVE / NO DATA (no automatic tier B), per-session PASS / FAIL / INCONCLUSIVE / NOT RUN, the session requirements and eight stop conditions.
- **Open:** ChatGPT's answer to request #33, then the owner's approval of M1 tier A; the second player's copy.

## 2026-10-27: Review #30 follow-up: GOG build pin and M1 criteria (`feature/d008-gog-inventory`, PR #26)

- **Input:** [PR #26 review #30](https://github.com/M0nsT3rG/ProjectMordor/pull/26); the owner asked for all of it, after review #29, which went to the KI-35 session.
- **FB-30-04:** `tools/environment/check_gog_build.py` (6 self-checks) is run by `scripts/inventory.ps1`.
  - It checks the store metadata, the exe SHA-256 and FileVersion, and the absence of crack, emulator and proxy-DLL files, against the D-008 pin.
  - It writes `evidence/gog-build.json` without paths.
  - Exit 3 blocks the inventory.
- **The check failed on its first real run:** the official Expansion Pass installer had replaced the exe after the morning check (F-ENV-012).
  - Same FileVersion, new SHA-256 `F16DC949…`, compiled 2022-09-09, the same header layout and no wrapper indicators.
  - The High Resolution Texture Pack was installed too.
  - The pin is unchanged: re-pinning is the owner's decision.
- **FB-30-02/03:** `docs/m1-criteria.md` is a draft. It defines one value (the player position); tier A (the game's own files, without changing them) before tier B (read-only process observation, separate approval); the fingerprint, the evidence, privacy, go and no-go, and the stop conditions. M1 has not started.
- **D-008 amended (owner, 2026-10-27):** pin the installed set (base + Expansion Pass + HD Texture Pack, exe `F16DC949…`). `check_gog_build.py` now also requires the two add-ons with the pinned buildId (7 self-checks) and passes on this machine.
- **F-ENV-013:** the owner reports that the pinned set (with the Expansion Pass) starts and plays with Steam and GOG Galaxy closed.
- **Open:** approving M1 tier A; the second player's copy.

## 2026-10-27: Read-only check of the owner's GOG copy (`feature/d008-gog-inventory`)

- **Input:** the owner bought and installed the GOG release and asked for a read-only check. Nothing was modified, executed or recorded about its location.
- **F-ENV-009:**
  - GOG store metadata is present (gameId 1324471032, buildId 55823353107546309), with GOG's uninstaller and no crack or emulator files.
  - The executable is FileVersion 1.0.2445.0, 44,473,856 bytes, SHA-256 `8F84F4A6…`: a different build from the Steam one (1.0.8874.0).
  - Valve's signed `steam_api64.dll` is shipped.
- **F-ENV-010:** the Phase 1 header tool (`pe_inventory.py`, as for F-ENV-003) finds the entry point in `.text`, a conventional layout and no `.bind`. The Steam build enters in `.bind`. That is the absence of wrapper indicators, not a proof that no protection exists.
- **F-ENV-011:** the owner reports that the GOG copy starts and plays with Steam and GOG Galaxy closed.
- **D-008 recorded (owner, 2026-10-27):** the target platform is the GOG release, exe 1.0.2445.0, SHA-256 `8F84F4A6…`. With D-007, **G0 is open**: CLAUDE.md, the feasibility matrix, the roadmap, KI-01 and CURRENT_TASK are updated. All rules are unchanged, and the Steam build stays out of scope.
- **Open:** designing M1 (read-only observation) with go/no-go criteria; the second player's GOG copy. Any later game-side work targets one build: offsets and layouts do not carry across.

## 2026-10-26: Gate item 2 fixes and the second integration-gate run (`feature/review23-integration`)

- **Input:** the owner asked to fix Codex's 3 P2 findings on the candidate (passes 6 and 7 on `2217085`), with regressions, and then to get two consecutive clean Codex passes. Record: EXP-038.
- **Fixes**, each with a regression that fails without it (red-before checked by restoring the old code and rebuilding):
  - **Client:** after the transport explicitly reported the host peer gone, the client reconnects on a new transport peer, on any backend (`client_renews_udp_peer_the_transport_reported_gone`).
  - **Secure transport:** `DtlsSession::send` returns the inner `SendResult`, and the decorator passes it on. A refused reliable frame is not queued (`secure_send_reports_the_inner_refusal`, both backends).
  - **Host:** grace expiry never closes a peer that a connected player uses (`grace_expiry_never_closes_a_peer_a_new_player_reuses`).
  - **mordor-client:** the event-order check follows the stream (`client/event_order.hpp`, `event_order_check_follows_the_stream`). The combined pass on `951fd77` found this one.
- **Gate run on `9904293`:** suites passed (clean); asymmetric 4/4; secure E2E 4/4; soak v6 12/12; dependencies unchanged; merges cleanly.
  - harsh-gns-secure passed with a 1853 ms recovery. That is one sample, and KI-35 stays open.
- **Combined pass on `c011a0c`:** 1 more P2 of the same kind. When a timed-out player resumes with its token from another address, the identity migration closed the old peer, which a new player from that address may use. The host now has one guard, `close_unshared_peer`, used for every close it makes (expiry, migration, lingers), with the regression `token_migration_never_closes_a_peer_a_new_player_reuses` (red-before checked). So the gate battery reruns on the new head.
- **Combined pass on `4a7ec3f`:** a `Client` reused with `close()`, `start()` and `connect()` inherited "transport up" and its old peer. On a backend that reports its own establishment, the session window then started before the new transport was up. `start()` now resets both; the regression `reused_client_waits_for_its_new_transport` (GNS, red-before checked) is skipped on UDP, which has nothing to wait for. The suite run started on `4a7ec3f` was stopped (obsolete head) and is kept as incomplete.
- **Combined pass on `ea881d0`:** events and acks did not name their stream. On UDP a leave and rejoin in the same session reuses the peer and restarts ids at 1, so a delayed event of the old stream was delivered as the new player's, and a delayed ack erased the new player's events. **Protocol v4** puts the player in `GameEvent` and `EventAck`, and both sides drop the other player's. The regressions `client_never_delivers_an_earlier_players_delayed_event` and `an_earlier_players_delayed_ack_never_erases_the_new_players_events` are red-before checked.
- **Combined pass on `0dc1929`:** the leave handshake had the same gap. A delayed `DisconnectAck` of an earlier leave confirmed a later one, and a delayed `Disconnect` of an earlier player removed the new one. `Disconnect` and `DisconnectAck` now carry the player too, checked on both sides; the two regressions above cover it (red-before checked).
- **Combined pass on `1f209fe`:** a build directory first configured without GNS kept `VCPKG_MANIFEST_INSTALL=OFF` in its cache, so enabling GNS later failed in `find_package`. `CMakeLists.txt` now forces it ON with GNS. Reproduced by hand in a scratch directory (configure OFF, then ON): the old file fails, the new one installs and configures. There is no automated test, because it would need a vcpkg install per run.
- **Combined pass on `40b02c5`:** `test.ps1 -E2E` passed the host and client log paths unquoted, so a results directory with a space failed as an infrastructure failure. They are quoted now, and the three E2E waits are bounded (`Wait-Bounded`, which dumps and kills a hung process) instead of unbounded `WaitForExit()`. `test-repeat-capture.ps1` runs a real E2E under a path with a space (red-before checked).
- **Gate run 3 on `3607613` (EXP-039):** asymmetric 4/4, secure E2E 4/4, soak 12/12. The suites FAILED once: `packet_loss_does_not_break_session` on GNS. It was not reproduced in 40 repetitions (1 failure in 62 recorded GNS runs) and is filed as KI-43, a timing margin like KI-41; the run stays failed as recorded. The suites run again on the final head.
- **Codex on `01c2645`:** pass 1 was clean, pass 2 found a P2. `secure-e2e.ps1` ran its second host for a fixed 14 s, so a slow start could leave a negative case facing a stopped host, and its exit 1 then passed as a rejection. host2 now runs until a stop file. `run.json` records that host2 was alive after each negative, and `check_secure.py` gates on that plus `pending_opened >= 2`. Both transports passed. The checker self-test `test_check_secure.py` was already failing at HEAD (its fixture lacked `barrier` and `exit_status`) and nothing ran it; it is fixed, and `test.ps1` now runs both gate-checker self-tests. Suites on `01c2645` passed clean (`tests-2026-10-26-084752-01c2645`).
- **Item 2 reached on `3c618d9`:** two consecutive clean Codex passes on the unchanged head. Gate run 4 on it (EXP-040) passed items 1-7: suites clean, asymmetric 4/4, secure E2E 4/4, soak 12/12 (harsh-gns-secure 2322 ms), mergeable.
- **Item 9:** the owner approved in chat ("do the merge"), and PR #24 was retargeted to `main` and merged with a merge commit, never squashed or rebased: the evidence cites commit hashes. KI-35 and KI-43 stay open. Two consecutive clean passes on the final head are still needed, and then the owner's decision.

## 2026-10-25: Review #23 follow-up and the first integration-gate run (`feature/review23-integration`, stacked on #23)

- **Input:** [PR #23 review](https://github.com/M0nsT3rG/ProjectMordor/pull/23); the owner asked for all of it. Record: EXP-035.
- **Changes:**
  - Restoration manifest v2, checked per file against `run.json`; 6 vs 7 reconciled.
  - `docs/integration-gate.md`; `utf8-range` licence corrected to MIT.
- **The gate's combined review against `main`** (Codex, 5 passes) found **12 real defects** that the per-PR reviews had missed. All are fixed, each with a regression that fails without the fix:
  - an unbounded pre-authentication peer table (UDP) and unbounded silent incoming connections (GNS);
  - the client event stream for a new player; terminal-state reconnects; a double connect; a stale `leave_confirmed`;
  - a failed DTLS handshake leaving the inner peer open; the idle check order; secure `received` never counted;
  - non-ASCII command-line paths (UTF-8 code-page manifest); unquoted key paths; unbounded warm-ups.
- **Gate run on `508ae40` (clean):** suites passed; asymmetric 4/4; secure E2E 4/4; soak 11/12 (harsh-gns-secure 3432 ms, KI-35); merges cleanly.
- **Gate not yet passed:** Codex pass 6 is pending (usage limit), and the owner's approval is needed.
- **Pitfalls found:** `Copy-Item` keeps old timestamps, so MSBuild runs a stale binary in red-before checks; a heredoc `\n` in C++ again.
- **Remaining:** gate items 2 and 9; KI-35, KI-42, KI-41, KI-28b, KI-32; D-008 / G0.

## 2026-10-25: Review #22 follow-up (`feature/review22-escaping`, stacked on #22)

- **Input:** [PR #22 review](https://github.com/M0nsT3rG/ProjectMordor/pull/22); the owner asked for all of it. Record: EXP-034.
- **Owner actions:** PRs #1–#21 closed without merging; PR #22 retargeted to `main` as the consolidated PR; branches kept.
- **Changes:**
  - The runner escapes every JSON string field. An exception message with a newline used to break the file.
  - Unit test, plus an end-to-end check with the real binary.
  - Restoration manifest with SHA-256 digests: 6 runs restored, 0 missing. EXP-030/031/032 annotated; digest test added.
- **Tests:** rebuilt; full run passed; all self-tests pass; Codex found no regression.
- **Remaining:** D-008 and G0; KI-35, KI-42, KI-41, KI-28b, KI-32.

## 2026-10-25: Review #21 follow-up (`feature/review21-identity`, stacked on #21)

- **Input:** [PR #21 review](https://github.com/M0nsT3rG/ProjectMordor/pull/21); the owner asked for all of it. Record: EXP-033.
- **Changes:**
  - FAILED cases fail the run even with ctest exit 0.
  - The runner writes `"suite"` (`--suite`) and `test.ps1` checks it.
  - Quoted and spaced or non-ASCII paths are redacted.
- **Codex found a lost-evidence bug:** `.gitignore`'s `build-*/` hid every `build-gns-iter-1/`, so no GNS invocation copy since review #18 had been committed. The ignore is now anchored, the 7 directories are committed from disk, and a regression test was added.
- **Owner decision:** D-007 recorded (risk accepted by the owner, no legal advice). D-008 is pending; G0 is closed.
- **Tests:** rebuilt; full run passed; all self-tests pass.
- **Remaining:** D-008, then G0; KI-35, KI-42, KI-41, KI-28b, KI-32.

## 2026-10-25: Review #20 follow-up (`feature/review20-validation`, stacked on #20)

- **Input:** [PR #20 review](https://github.com/M0nsT3rG/ProjectMordor/pull/20); the owner asked for all of it. Record: EXP-032.
- **Changes:**
  - A run stopped by an error ends as `infrastructure_failure` with a path-free `reason`.
  - Result files are validated; invalid ones are kept apart and fail the run.
  - Recent-run lines show commit, clean or dirty, `INVALID` and the reason; `-GnsBuildDir`.
- **Defect found while testing:** PowerShell 5.1 `@($x | ConvertFrom-Json)` wraps an array as one element. Fixed before any real run used it; the stand-in ctest now writes two cases per suite.
- **Also today:** the README on `main` has a progress section (about 14%, read as 10–15%; in-game co-op 0%, blocked by G0).
- **Tests:** harness all passed; report 12/12; full `-GnsBuild -Repeat 2 -E2E` passed.
- **Remaining:** KI-35 (GNS phases), KI-42, KI-41, KI-28b, KI-32.

## 2026-10-25: Review #19 follow-up (`feature/review19-freshness`, stacked on #19)

- **Input:** [PR #19 review](https://github.com/M0nsT3rG/ProjectMordor/pull/19); the owner asked for all of it. Record: EXP-031.
- **Changes:**
  - Expected result files are deleted before each invocation, with no timestamp check. A suite that writes nothing is `missing` and fails the run, even with ctest exit 0. `-GnsBuild` always runs.
  - `report.py` lists the 5 newest runs with every invocation's exit code.
  - The harness has 21 checks, including a missing suite, a stale file and a positive control.
- **Review:** an independent subagent (Codex was at its usage limit) found nothing significant; its 4 points are fixed.
- **Tests:** two full `-GnsBuild -Repeat 2 -E2E` runs, all passed; tool self-tests pass.
- **Remaining:** a Codex review once the limit resets; KI-35 (GNS phases), KI-42, KI-41, KI-28b, KI-32.

## 2026-10-25: Review #18 follow-up (`feature/review18-repeat`, stacked on #18)

- **Input:** [PR #18 review](https://github.com/M0nsT3rG/ProjectMordor/pull/18); the owner asked for all of it. Record: EXP-030.
- **Changes:**
  - Every `-Repeat` invocation is kept in `iter-<n>/` with its exit code (`ctest_runs`); fail-then-pass self-check `scripts/test-repeat-capture.ps1`.
  - `report.py` shows each suite's run result, commit and dirty flag.
  - The wait budget is qualified as a budget; a creation snapshot is the evidence present at commit.
- **Review:** Codex, no regression. A later code review of PR #19 found 2 more: `run.json` is now written after each invocation, and `[CmdletBinding()]` refuses misspelled parameters. 5 harness checks were added.
- **Tests:** `test.ps1 -GnsBuild -Repeat 2 -E2E`, then `-GnsBuild -E2E` after the review fixes: all passed; both self-checks pass; report self-tests 8/8.
- **Incident:** the red-before check ran the old `test.ps1`, which ignored `-LogsDir` and wrote an empty run directory into `evidence/logs`. It was deleted as an artifact of the check.
- **Remaining:** KI-35 (GNS phases), KI-42, KI-41, KI-28b, LAN test, external review.

## 2026-10-25: Review #17 follow-up (`feature/review17-evidence`, stacked on #17)

- **Input:** [PR #17 review](https://github.com/M0nsT3rG/ProjectMordor/pull/17), FB-17-01/02; the owner asked for all of it. Record: EXP-029.
- **Changes:**
  - The dump limit is documented as additional to each wait.
  - `test.ps1` writes an immutable per-run directory with `run.json` and `e2e/`. `check_test_transport.py --dir`; `report.py` reads run directories.
  - `tools/evidence/recover_dated.py` recovered 156 overwritten files for 20 records from git into `evidence/logs/recovered/`, with provenance, and repointed the records.
  - The validator rejects date-named paths.
- **Review:** Codex, first review since KI-09 was fixed. Pass 1 found 2 P2: E2E files were missed by the recovery and the rule, and `report.py` did not list the new E2E directories. Both fixed. Pass 2: clean.
- **Tests:** full run (12 suites, E2E, smoke, self-check) and a quick UDP run, each in its own directory; tool self-tests pass.
- **Remaining:** KI-35 (GNS phases), KI-42, KI-41, KI-28b, LAN test, external review.

## 2026-10-25: Review #16 follow-up (`feature/review16-followup`, stacked on #16)

- **Input:** [PR #16 review](https://github.com/M0nsT3rG/ProjectMordor/pull/16), FB-16-01 to FB-16-03; the owner asked for all of it. Record: EXP-028.
- **Changes:**
  - `Invoke-BoundedDump`: the diagnostic runs in its own bounded process, and its tree is killed on expiry. The target is always killed, and a timed-out dump gives `hung_dump_timeout`. `hang-watch.ps1` uses the same helper.
  - `scripts/test-wait-bounded.ps1` (12 checks, run by `test.ps1`).
  - `compare_arms.py`: no verdict without usable runs or without a finite B median; 4 regressions.
  - GNS shutdown guard on every exit path of `mordor-host` and `mordor-client`; shutdown trace; a start-failure smoke test in `test.ps1 -GnsBuild`.
- **Review:** an independent subagent reviewed the diff. It found nothing significant, and 5 of its 7 low findings were applied.
- **Tests:**
  - suites ×3 on both builds, all passed; start-failure smoke 9/9;
  - E2E; secure E2E UDP and GNS;
  - soaks moderate-gns-secure and moderate-udp-secure, 59/59 each.
- **Remaining:** KI-35 (GNS), KI-42, KI-41, KI-28b, LAN test, external review.

## 2026-10-25: Review #15 follow-up (`feature/review15-followup`, stacked on #15)

- **Input:** [PR #15 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/15), FB-15-01 to FB-15-05; the owner asked for all of it. Record: EXP-027.
- **Changes:**
  - F-GNS-002 (GNS dispatches by source address), capability `proves_return_routability`;
  - `--dtls-skip-cookie-if-routable` (off by default) with two security tests;
  - KI-42: `MORDOR_GNS_TRACE`, an unrouted-callback counter, an endpoint-reuse stress test;
  - KI-40 root cause and fix (`gns_shutdown_for_exit`);
  - `Wait-Bounded` and `exit_status`; the effective skip recorded in `run.json`;
  - predeclared EXP-027.
- **Tests:**
  - suites, both passes on both builds: all passed, transport check OK;
  - E2E; asymmetric 4/4; secure E2E 4/4.
- **Soaks:**
  - EXP-027: B not adopted (median 1992 → 2274 ms; failures 3 → 1; DTLS phase −1 RTT);
  - battery: 12/12 plus repeats 8/8;
  - pooled at the head: GNS 3/15, UDP 0/5 failures.
- **Incident:** the first battery runner was killed by a session restart during the first EXP-027 run (kept, not counted). The rest was rerun from a detached process.
- **Remaining:** KI-35 (GNS), KI-42 (not reproduced, instrumented), KI-41, KI-28b, LAN test, external review.

## 2026-10-25: Reviews #13/#14 follow-up (`feature/review13-14-followup`, stacked on #14)

- **Input:** ChatGPT's comments on PRs #12, #13 and #14 (FB-13-01..03, FB-14-01..03); the owner asked for all of it. Record: EXP-026.
- **Changes:**
  - ctest transport pinning and a results check;
  - aborted runs counted apart;
  - `timeline.py --summary`;
  - `scripts/hang-watch.ps1`;
  - the smoke-test contract;
  - the secure E2E barrier and `--stop-file`;
  - predeclared EXP-026.
- **Tests:**
  - pass 1: all passed except `contract_endpoint_reuse_gets_new_peer_generation` on GNS (KI-42); pass 2: all passed;
  - E2E; asymmetric 4/4; secure E2E 4/4 plus 2 barrier checks and 1 deliberate negative.
- **Soaks:**
  - EXP-026: 16/16 passed, B not adopted;
  - GNS distribution: 6/8;
  - battery: 12/12 plus repeats 4/8 (GNS 1/4, UDP 3/4 passed).
  - Pooled at the head: GNS 5/13, UDP 1/13 failures.
- **Remaining:** KI-35 (GNS), KI-40 (not reproduced), KI-41/42 (rare flakes), KI-28b, LAN test, external review.

## 2026-10-24: Fix for the PR #12 follow-up review (`fix/linger-eviction-rejoin`, stacked on #13)

- **Input:** ChatGPT's inline P2 on PR #12. Linger-cap eviction could close a player who had rejoined on the same peer.
- **Change:** `Host::accept()` clears the peer's linger entry; the cap path keeps the active-peer guard. New test `rejoined_player_survives_linger_cap_pressure`, which failed before the fix on both backends.
- **Tests:**
  - pass 1: all passed;
  - pass 2: all passed except `heavy_loss_and_duplication_never_crash` on GNS (KI-41; 5/5 on repeat);
  - E2E passed;
  - secure E2E: GNS passed; UDP failed once (late client launch, KI-41 note), then 2/2 on repeat.
- **Remaining:** KI-35, KI-40, KI-41.

## 2026-10-24: Review #12 follow-up (`feature/review12-retry-experiment`, stacked on #12)

- **Input:** [PR #12 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/12), FB-12-01 to FB-12-05; the owner asked for all of it. Record: EXP-025.
- **Changes:**
  - ConnectRequest attempt logging; `--reconnect-retry-ms`; `soak.ps1 -ReconnectRetryMs`.
  - Predeclared retry experiment and `tools/soak/compare_arms.py`.
  - Lifecycle trace and GNS lifecycle test.
  - Departure tests.
  - F-GNS-001; cookie cost in the timeline.
  - The soak warm-up no longer aborts on stderr.
- **Tests:**
  - dtls {'PASSED': 56}; integration {'PASSED': 53}; integration-gns {'PASSED': 38, 'SKIPPED': 15}; network {'PASSED': 17}; network-gns {'SKIPPED': 5, 'PASSED': 12}; transport {'PASSED': 18}; transport-both {'PASSED': 21}; unit {'PASSED': 17}; gns {'PASSED': 7}.
  - ×2, plus E2E; asymmetric 4/4; secure E2E 4/4.
- **Soaks:**
  - experiment 19 runs (1 aborted): B not adopted;
  - final battery **20/20**.
- **Problems encountered:**
  - the first battery attempt was contaminated (environment variable leak) and restarted;
  - one soak stopped after its warm-up;
  - KI-40 not reproduced.
- **Remaining:** KI-35 (about 29% / 13% failure on the harsh secure profiles), KI-40, KI-28b, LAN test, external review, debugger approval for KI-40 dumps.

## 2026-10-24: Review #11 follow-up (`feature/review11-gns-readiness`, stacked on #11)

- **Input:** [PR #11 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/11), FB-11-01 to FB-11-05; the owner asked for all of it. Record: EXP-024.
- **Changes:**
  - **Client DTLS starts on the inner `Connected`** (after measuring it). An inner connect failure is reported as `inner_connect_failed`.
  - The leave linger is bounded (16) and closed early on a client disconnect. A host `Disconnect` ends a leave.
  - `coalesce_records` option and truncation guard.
  - `soak.ps1 -SeedBase`; `run.json` `gate_version` 6.
  - Timeline: inner-connect timing.
- **Tests:**
  - dtls {'PASSED': 56}; integration {'PASSED': 50}; integration-gns {'PASSED': 35, 'SKIPPED': 15}; network {'PASSED': 17}; network-gns {'SKIPPED': 5, 'PASSED': 12}; transport {'PASSED': 18}; transport-both {'PASSED': 20}; unit {'PASSED': 17}; gns {'PASSED': 7}.
  - ×2, plus E2E; asymmetric 4/4; secure E2E 4/4.
- **Soaks:**
  - v6 battery at HEAD: **12/12**.
  - Repeats: harsh-gns-secure 3/5, harsh-udp-secure 4/5.
- **Problems encountered:**
  - KI-40 (exit hang, twice; it stopped one battery run, which was restarted);
  - one soak aborted after its warm-up (kept);
  - the runner died when `test.ps1` threw (fixed with `try`).
- **Remaining:** KI-35 (GNS connect under loss, DTLS back-off, 500 ms retry), KI-40, KI-28b, LAN test, external review.

## 2026-10-24: Review #10 follow-up (`feature/ki35-ki37-investigation`, stacked on #10)

- **Input:** [PR #10 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/10#issuecomment-6380695083), FB-10-01 to FB-10-05 and answer 5; the owner asked for all of it on this branch. Record: EXP-023.
- **Changes:**
  - FB-10-01: `EventAck` refused above the highest id the transport accepted (`event_acks_invalid`).
  - FB-10-02: an event counts as sent only on `Sent`; refused sends are retried with back-off.
  - Answer 5 / KI-30: stateless `DTLSv1_listen` cookie gate. Cookie-proven ClientHellos bypass the 64/s budget.
  - **Protocol v3:** `DisconnectAck`, client `Leaving` state (FB-10-04). The host keeps the transport peer of a cleanly-left player open for 1.5 s.
  - KI-35 instrumentation: `dtls_trace`, `clock` records, reconnect milestones, `player_stats`, transport stats; `tools/soak/timeline.py`.
  - KI-35 fixes from the traces: DTLS flight retransmissions coalesced into one datagram; ConnectRequest sent as soon as the transport is up.
  - Soak gate v6 (FB-10-05), fixed before its runs.
- **Tests:**
  - unit {'PASSED': 17}; integration {'PASSED': 44}; network {'PASSED': 17}; transport {'PASSED': 18}; transport-both {'PASSED': 20}; integration-gns {'PASSED': 30, 'SKIPPED': 14}; network-gns {'SKIPPED': 5, 'PASSED': 12}; gns {'PASSED': 7}; dtls {'PASSED': 50};
  - ×2 on both builds, plus E2E;
  - asymmetric 4/4; secure E2E 4/4; KI-27 reproducer 0 assertions with the 50 ms settle (10/10);
  - **v6 soaks 11/12** (harsh-gns-secure failed recovery, 4168 ms).
- **Problems encountered:**
  - the FB-8-01 test caught forged ClientHellos starving a real reconnect through the global rate limit (fixed);
  - one soak was aborted when the battery was restarted outside the tool time limit (kept);
  - the GNS case of the flight-coalescing test cannot be reproduced with process-global impairment (NOT RUN).
- **Independent review** (an agent; Codex fails here, EXP-008). Fixed, with tests:
  - cookie expiry and separate budgets;
  - events no longer overtake a refused or backing-off one (41 gaps to 0);
  - repeated leave acks only to peers in their leave window, rate-limited;
  - the client's leave loop bounded.
- **After the fixes:**
  - the soak battery above ran on `04d153a`, before them;
  - they were verified by the suites, secure E2E and two secure soaks (EXP-023).
- **KI-39 (resolved):** test timing depended on the system timer resolution. Windows 11 ignores `timeBeginPeriod` for windowless processes, so the test runner now also opts out of that throttling. `test.ps1` also used to stop before copying the GNS results when ctest failed; fixed.
- **Remaining:** KI-35 on GNS (KI-38 hypothesis), KI-28b, LAN test, external review.

## 2026-10-24: Review #9 follow-up (`feature/secure-liveness`, stacked on #9)

- **Input:** [PR #9 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/9#issuecomment-6379227966), FB-9-01 to FB-9-05; the owner asked for everything, including FB-9-02.
- **Changes:**
  - Authenticated-only liveness.
  - Handle-based key loading; temp-then-move write.
  - Misuse tests; ClientHello rate limit.
  - **Protocol v2** application events (D-017) in Host/Client and the executables.
  - Soak gate v5 (realized impairment and events).
  - DTLS retransmit cap 1 s.
  - Diagnostics: handshake time and retransmits in `secure_stats`.
- **Tests:**
  - unit {'PASSED': 17}; integration {'PASSED': 40}; network {'PASSED': 17}; transport {'PASSED': 18}; transport-both {'PASSED': 20}; integration-gns {'PASSED': 26, 'SKIPPED': 14}; network-gns {'SKIPPED': 5, 'PASSED': 12}; gns {'PASSED': 7}; dtls {'PASSED': 48};
  - ×2, plus E2E;
  - asymmetric 4/4; secure E2E 2/2;
  - **v5 soaks 10/12**.
- **Problems encountered:**
  - three defects found and fixed (rate-limit starvation, head-of-line `Disconnect`, events stalled behind a discarded first event);
  - KI-35 and KI-37 open;
  - symlink check not run.
- **Remaining:** KI-35, KI-37, KI-30, LAN test, independent review.

## 2026-10-24: Review #8 follow-up (`feature/secure-provisioning`, stacked on #8)

- **Input:** [PR #8 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/8#issuecomment-6377503056), FB-8-01 to FB-8-05, on the owner's go-ahead ("faz tudo"). Also: the KI-27 upstream draft was refined, approved and posted as ValveSoftware/GameNetworkingSockets#458 (EXP-020).
- **Changes:**
  - Cookie-aware pending replacement and a reserved reconnect slot.
  - Secure reliable channel (in-flight cap 32).
  - Idle timeout with keepalives.
  - `mordor-keygen`, DPAPI key files with a user-only ACL, public invitations, fail-closed `--secure-*`.
  - `scripts/secure-e2e.ps1`; `soak.ps1 -Secure`.
  - Fixes: GNS pre-close message delivery, GNS callback drain, decorator delivery before disconnect, DTLS time propagation.
- **Tests:**
  - unit {'PASSED': 16}; integration {'PASSED': 34}; network {'PASSED': 16}; transport {'PASSED': 18}; transport-both {'PASSED': 20}; integration-gns {'PASSED': 20, 'SKIPPED': 14}; network-gns {'SKIPPED': 5, 'PASSED': 11}; gns {'PASSED': 7}; dtls {'PASSED': 42};
  - soak v4 43/43 × 12 (6 plaintext, 6 secure);
  - secure E2E 2/2; asymmetric 4/4;
  - KI-27 stress 0.
- **Problems encountered:**
  - five defects (EXP-021);
  - three secure UDP soaks invalid for latency (kept).
- **Remaining:** KI-30, KI-28, KI-32, LAN test, independent review.

## 2026-10-24: Review #7 follow-up (`feature/secure-transport`, stacked on #7)

- **Input:** [PR #7 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/7#issuecomment-6376769358), FB-7-01 to FB-7-05, on the owner's go-ahead; also prepare the KI-27 upstream draft.
- **Changes:**
  - `SecureTransport` decorator (D-015): Host/Client over DTLS, no plaintext fallback.
  - Caps and expiry for pending sessions.
  - `DTLS_set_timer_cb` back-off.
  - Binding reply rate limit; multi-pin.
  - KI-27 back to open; minimal reproducer `tools/ki27-repro`; upstream draft `docs/upstream/gns-ki27-issue.md` (not published).
- **Tests:**
  - unit {'PASSED': 16}; integration {'PASSED': 34}; network {'PASSED': 16}; transport {'PASSED': 17}; transport-both {'PASSED': 19}; integration-gns {'PASSED': 20, 'SKIPPED': 14}; network-gns {'SKIPPED': 5, 'PASSED': 11}; gns {'PASSED': 7}; dtls {'PASSED': 30};
  - ×2, plus E2E;
  - soak v4 43/43 × 6;
  - asymmetric 4/4;
  - KI-27 stress 0/10 processes.
- **Problems encountered:** with the default OpenSSL back-off, 2 DTLS tests failed (one intermittently); fixed with the timer policy.
- **Remaining:** KI-30 (stateless cookie), KI-31 (executable provisioning), independent review, owner decision on the upstream post.

## 2026-10-24: Review #6 follow-up (`feature/dtls-transport`, stacked on #6)

- **Input:** [PR #6 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/6#issuecomment-6376091101), FB-6-01 to FB-6-05, on the owner's go-ahead.
- **Changes:**
  - `DtlsSession` over `Transport::Unreliable` with a fail-closed invitation binding, HVR cookies and a pending session for same-address reconnects.
  - KI-27 trigger localised and empirically mitigated (still open, FB-7-03): process-lifetime GNS runtime plus 50 ms settle; assertion counter and regression gates.
  - `scripts/asymmetric.ps1` with `tools/e2e/check_asymmetric.py`.
  - `--connect-deadline-s` bounded to [1, 120] s.
  - Wording sweep.
- **Tests:**
  - unit {'PASSED': 16}; integration {'PASSED': 34}; network {'PASSED': 16}; transport {'PASSED': 17}; transport-both {'PASSED': 19}; integration-gns {'PASSED': 20, 'SKIPPED': 14}; network-gns {'SKIPPED': 5, 'PASSED': 11}; gns {'PASSED': 7}; dtls {'PASSED': 17};
  - ×2 in both builds, plus E2E;
  - soak v4 43/43 × 6;
  - asymmetric 4/4.
- **Problems encountered:**
  - DTLS transport first run 15/16;
  - a weak first test draft;
  - KI-27 probe numbers exist only as console transcriptions.
- **Remaining:** game traffic over DTLS, a stateless cookie, an independent review (KI-29).

## 2026-10-24: Review #5 follow-up (`feature/gns-hardening`, stacked on #5)

- **Input:** [PR #5 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/5#issuecomment-6375464043), FB-5-01 to FB-5-05; the owner approved the implementation and the DTLS proof test.
- **Changes:**
  - Counter semantics fixed and documented (wire-level diagnostics).
  - `connect_deadline_us` at the session layer.
  - GNS impairment single-config guard (`set_impairment` returns bool; `impairment_refused`).
  - 8 `_over_transport` substitute tests.
  - `docs/test-coverage.md`.
  - `tests/dtls` proof (OpenSSL is now a direct dependency of the GNS build).
- **Tests:**
  - UDP: unit 15, integration 34, network 16, transport 17.
  - GNS: transport 17, integration 20 P / 14 SKIP, network 11 P / 5 SKIP, spike 7, dtls 8.
  - ×2, plus E2E. Soak v4 43/43 × 6.
- **Problems encountered:**
  - a false GNS pass exposed by the guard;
  - the DTLS first run failed (DANE);
  - two substitute drafts failed on GNS (the old connection is really closed);
  - a pre-existing GNS teardown assertion (KI-27).
- **Remaining:** DTLS integration and an independent review (KI-29); application-level delivery (KI-28).

## 2026-10-23: GameNetworkingSockets adapter (`feature/gns-adapter`, stacked on #4)

- **Input:** ChatGPT recommendation after review #4 (priorities 1–3).
- **Changes:**
  - `GnsTransport` behind contract v2 (`src/network/gns_transport.*`); capabilities extended with `exact_loss_accounting` and `per_instance_impairment`; new counter `closed_with_unacked_reliable`.
  - Client: transport `Disconnected` handled in every state; reconnect opens a new connection; D-014 (the transport owns establishment). Clean `Disconnect`/`HostShutdown` sent reliably where supported.
  - `--transport udp|gns` on host and client; `MORDOR_TRANSPORT` for tests; `UDP_ONLY(reason)` → SKIPPED.
  - 5 new reliable-backend contract tests; a symmetric-outage integration test.
  - Soak gate v4 (transport declared and matched); `soak.ps1 -Transport`; `test.ps1 -GnsBuild`; `report.py` exact suite matching and a Skipped column.
  - `docs/d013-evaluation.md`.
- **Tests:** first GNS runs failed (contract 7/10, integration 9 P / 3 F, network 6 P / 3 F), preserved as `*-first-run-*` logs. Final: UDP unit 15, integration 26, network 12, transport 15; GNS transport 15, integration 12 P / 14 SKIP, network 8 P / 4 SKIP, spike 7; ×2, plus E2E. Soak v4 43/43 on 3 profiles × 2 transports.
- **Problems encountered:** 8 defects (EXP-016), two failed soaks preserved (lan timeouts, harsh p99 313 ms).
- **Remaining:** D-013 owner decision; asymmetric GNS coverage (KI-24).

## 2026-10-23: Review #4 triage, Transport contract v2 (`feature/transport-interface`)

- **Input:** [PR #4 comment](https://github.com/M0nsT3rG/ProjectMordor/pull/4#issuecomment-6373680146), FB-4-01 to FB-4-05.
- **Changes:**
  - Contract v2: `PeerId` + generation, lifecycle events, delivery classes, `SendResult`, failure counters, `flush()`.
  - `UdpTransport` v2 (`udp_transport.cpp`).
  - Host and client migrated to `PeerId` and events; transport disconnects are handled; send errors are counted.
  - `ImpairedSender` reports per-send outcomes and delayed failures.
  - New `tests/transport` contract suite.
  - Auth state machine §4a; D-013 options revised.
- **Tests:**
  - UDP suites and harness unchanged, ×3 in both builds.
  - Contract 10/10, and the mutation check failed exactly the 2 targeted tests.
  - E2E passed; moderate soak v3 41/41 (EXP-015).
- **Problems encountered:** a draft double poll in `Host::update`, caught before compiling.
- **Remaining:** GNS adapter (must pass `tests/transport` unchanged); D-013.

## 2026-10-23: Review #3 triage, Transport interface (`feature/transport-interface`, stacked on #3)

- **Input:** [PR #3 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/3#issuecomment-6373375242), findings FB-3-01 to FB-3-06.
- **Changes:**
  - `net::Transport` + `UdpTransport` + factory; Host and Client run on the interface.
  - `ImpairedSender` takes any transmit function.
  - GNS auth facts verified in the library's headers and source.
  - `docs/authentication-design.md`.
  - GNS tests rewritten: RAII state guard, port retries, concurrent, multi-client, mixed, queue pressure, asserted security baselines.
  - D-012 updated; D-013 pending.
- **Tests:**
  - No UDP test or harness file changed by the refactor; only `tests/gns/` was rewritten, deliberately.
  - Unit, integration and network 52/52 ×3 in both builds; E2E passed; lan soak v3 41/41, metrics unchanged.
  - GNS 7/7 ×3, and 21/21 across 3 parallel instances (EXP-014).
- **Problems encountered:**
  - Git checkout converted files to CRLF, which broke a PowerShell exact-match edit script. Nothing was written, because the script aborts on a missing anchor; I switched to the Edit tool.
  - FB-3-06 changed the planned impairment layering for GNS.
- **Remaining:**
  - GNS adapter (KI-22).
  - D-013 crypto library, then the authenticated-peer implementation and its tests (KI-21).

## 2026-10-23: Review #2 follow-up, soak gate v3 (`feature/realtime-soak`)

- **Input:** [PR #2 comment 6372799837](https://github.com/M0nsT3rG/ProjectMordor/pull/2#issuecomment-6372799837).
- **Changes:**
  - Gate v3: valid and unique manifest names; a known scenario; manifest and stall plan must equal the scenario.
  - Bandwidth gates relabelled as mean-rate.
  - 1 s window peak/p95/p99 reporting; client status lines carry cumulative bytes.
  - `soak.ps1` writes `scenario` and `-g3` run directories.
- **Tests:**
  - Analyzer self-checks 17/17 (6 new).
  - Suites 52/52 ×3 (built in a separate git worktree while GNS compiled).
  - v3 soak: lan, moderate and harsh at 120 s, 41/41 gates each (EXP-012).
- **Problems encountered:**
  - The new self-checks found that a non-string client name crashed the analyzer. Fixed.
  - Burst peaks exceed the mean ceiling (KI-20).

## 2026-10-23: ChatGPT review #2 triage and fixes (`feature/realtime-soak`)

- **Input:** [PR #2 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/2#issuecomment-6372084762), findings FB-2-01 through FB-2-06. The triage is in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md).
- **Changes:**
  - **Analyzer gate v2 (FB-2-01, FB-2-03):** exact participant manifest; coverage minimums; bandwidth and correction ceilings derived from the design.
  - **Evidence immutability:** `soak.ps1` writes fresh timestamped run directories, and the analyzer never overwrites a `summary.json`.
  - **Client stale-notice guard (FB-2-02):** a `Disconnect` that is not newer than the current accept is ignored.
  - **Telemetry:** correction percentiles and `stale_notices`; the host logs `timeout_notices`.
  - **FB-2-04 and FB-2-06:** labels and comments.
- **Tests:**
  - 3 fake-host integration tests; 2 of them failed before the guard.
  - 1 real-host smoke test, which does not discriminate (no late notice occurred).
  - 8 new analyzer self-checks (11 total).
  - Suites: unit 15, integration 25, network 12, all passing ×3 plus E2E.
  - Soak gate v2: lan, moderate and harsh at 120 s, 36/36 gates each (EXP-011).
- **Problems encountered:**
  - Analyzer v1 was shown to pass a run where client2 never started.
  - The old `soak.ps1` emptied the run directory, so it would have destroyed evidence on a repeat run.
  - The real-host notice test could not reproduce a late notice.
  - The host's `timeout_notices` was not logged during the v2 soak runs; it was added afterwards.
- **Paused work:** the reliable event channel is a WIP on local branch `feature/reliable-events` (commit `187a121`, not pushed). The transport choice now waits for the user (see below).
- **Remaining:** FB-2-05 stall coverage; harsh recovery variance (KI-18); shed gate (KI-19); transport decision.

## 2026-10-23: Real-time soak test (`feature/realtime-soak`, stacked on PR #1)

- **Changes:**
  - Host and client telemetry.
  - `--stall-at-s` / `--stall-s` client options.
  - `scripts/soak.ps1` with 3 profiles.
  - `tools/soak/analyze.py`, gates fixed before the first run, with 3 self-checks.
  - `docs/soak-testing.md`.
  - **KI-12 fix:** the host timeout notice, and the client reconnecting on it.
  - **KI-02 fix:** a high-resolution waitable timer in both executables.
  - TEST_RESULTS gains a real-time soak section.
- **Tests:**
  - 2 new integration tests: `timed_out_client_is_told_and_reconnects_fast` and `timeout_notice_is_rate_limited_and_scoped`.
  - 48/48 simulated-clock tests on 3 or more runs; E2E passed.
  - Soak: lan, moderate and harsh at 120 s each, all gates passed (EXP-010).
- **Problems encountered:** the first soak smoke run **failed** its recovery gate at 3085 ms against a 3000 ms limit (KI-12). It also showed about 50 ms of RTT overhead from sleep granularity (KI-02). Both were fixed and the failing run is preserved. The KI-12 tests were not written test-first (see review request #2 §5).
- **Remaining:** reliable event channel; desync digests; long soaks; the LAN test (needs approval).

## 2026-10-23: ChatGPT review #1 triage and fixes (`feature/ai-collaboration-bridge`)

- **Input:** [PR #1 review comment](https://github.com/M0nsT3rG/ProjectMordor/pull/1#issuecomment-6371152419), findings FB-1-01 through FB-1-07. The triage is in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md).
- **Changes:**
  - **Host step (FB-1-01):** exactly one timestep per tick, with backlog shedding above 2 queued inputs. Counted in `HostStats::shed_inputs`.
  - **Liveness (FB-1-02):** host and client refresh liveness only for messages valid in their direction.
  - **Evidence (FB-1-05):** `verification_scope` added to all findings and enforced by the validator.
  - **Reporting (FB-1-03):** simulated-clock and real-time results are labelled separately.
  - **Logs and E2E script:** logs are opened shareable (`cli::open_log`); `test.ps1` deletes stale E2E logs and does a warm-up run.
  - **Documentation:** protocol.md, decisions D-010, KNOWN_ISSUES.
- **Tests:** 8 new tests in total.
  - Integration (7): `input_burst_never_exceeds_one_timestep_per_tick`, `steady_inputs_are_never_shed`, `wrong_direction_messages_do_not_refresh_liveness`, `client_ignores_wrong_direction_messages_for_liveness`, `reconnect_token_migrates_slot_to_new_endpoint`, `reconnect_after_grace_expiry_gets_new_identity`, `client_survives_host_restart_with_new_session`.
  - Network (1): `jitter_never_exceeds_tick_movement_budget`.
  - These were written first. On the old code, 4 failed because of the real bugs; a 5th failed because of a test bug, since fixed.
- **Results:**
  - 46/46 passed (unit 15, integration 20, network 11) on 5 ctest runs plus 3 scripted runs.
  - E2E passed, including right after a relink (with the warm-up).
  - `test_report.py` 4/4. Evidence validator: 71 records, 0 errors.
- **Problems encountered:**
  - One new test was wrong; it was fixed, not the code.
  - `fopen_s` exclusivity made the host fail to open its log.
  - The old E2E wait polled a stale log.
  - EXP-006 was reproduced; its cause remains unknown.
  - A PowerShell 5.1 BOM was written into EXP-006.json and then removed.
- **Remaining:**
  - Real-time soak and telemetry (FB-1-03 P1).
  - Authentication and rate limiting before any WAN use (FB-1-02).
  - Reliable event channel (FB-1-04).
  - Timer fix, LAN test.

## 2026-10-23: ChatGPT collaboration bridge

- **Commits:** `e549d3c` (bridge) plus a follow-up docs commit, on `feature/ai-collaboration-bridge`.
- **Published:** the private repository https://github.com/M0nsT3rG/ProjectMordor and PR #1 https://github.com/M0nsT3rG/ProjectMordor/pull/1. The GitHub CLI was installed via winget with user authorisation, and the user signed in personally.
- **Changes:**
  - Added the `.ai-collaboration/` bridge files.
  - Added `tools/collab/report.py`, a local status and review-summary generator with no network access.
  - Added the collaboration rules to `CLAUDE.md`.
- **Tests:** ran `report.py` against the repository and checked its output against `git` and `evidence/logs`. The prototype suites were not re-run, because no C++ code changed.
- **Results:** see [TEST_RESULTS.md](TEST_RESULTS.md).
- **Problems:** the first device-flow login did not complete. On the second attempt the user entered the code manually.
- **Remaining:** wait for ChatGPT feedback on review request #1, then triage it in CHATGPT_FEEDBACK.md.

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
