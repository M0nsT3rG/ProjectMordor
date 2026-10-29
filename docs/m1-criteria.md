# M1 criteria: read-only observation of game state (tier A approved by the owner; amendment for EXP-051 approved 2026-10-28; second amendment for EXP-060 approved 2026-10-29, with its label fix)

- **Status: tier A approved, 2026-10-27** (updated for reviews #32–#37 and Codex passes 1–8; tier A predeclaration in §8). The owner approved §8 in chat on 2026-10-27, as of commit `6f2ed04` (code `e171fe0`). **Session 1 ran on 2026-10-28 (EXP-048, PARTIAL): INCONCLUSIVE, NO DATA not established**, because 1203 ETW events were lost and two permitted files were unreadable (KI-45; see "Session 1" at the end of §8). The series is stopped. **A §8 amendment for EXP-051** (bigger trace buffers, a Windows-1252 fallback, `gpu.nfo` while binary) was **approved by the owner on 2026-10-28**, as of commit `fe0af56` (code `322f02d`). **EXP-050 and the dry run passed elevated on 2026-10-28** (the owner's runs on clean `f2ef9bd`, session tools as at `322f02d`; 0 events lost under the flood and the mixed load), so EXP-051 session 1 ran the same day. **It left no evidence:** the trace analysis ran out of memory, Ctrl+C did not reach it, and the owner ended it (KI-45). EXP-051 is ABANDONED and the series is stopped. **A second amendment** streams the analysis. It changes only `netwatch.py`. **EXP-050 and the dry run passed elevated again on 2026-10-29** (the owner's runs on clean `7126ff0`, code `f1665ae`: 9/9 MATCH, 0 events lost under the flood and the mixed load; 11/11), and the owner **approved it on 2026-10-29** ("Aprovo"), as of commit `a8f5d5a` (code `f1665ae`). `tier_a.py` then turned out to still label its record "EXP-051"; the label now says "EXP-060", and the owner **approved that too on 2026-10-29** ("Aprovo"), as of commit `c4e9cce`. EXP-060 session 1 may run (§7 item 4). The next series is EXP-060 (§7 item 4). Tier B (below) needs a second, separate approval.
- **Sources:** reviews #30 (FB-30-01..04) and #32 (FB-32-01..04); the M1 row of [feasibility-matrix.md](feasibility-matrix.md); D-007 and D-008 in [decisions.md](decisions.md).
- **Every non-negotiable rule in `/CLAUDE.md` applies.** Nothing here relaxes them.

## 1. Build fingerprint (FB-30-01, FB-30-04)

- **Only the build pinned by D-008.** `tools/environment/check_gog_build.py` (run by `scripts/inventory.ps1`) must exit 0 (VERIFIED) at the **start and the end** of every M1 session.
  - It compares the store metadata of the base game and both add-ons, the exe FileVersion, and the SHA-256 of **every file in `x64`** as an exact list: a changed, missing or extra file (an exe, a DLL, a crack, a proxy DLL) is a mismatch.
  - Exit 3 (MISMATCH) means a different build or foreign files, and the session stops.
  - Exit 5 (INCOMPLETE) means something could not be read or parsed (FB-32-02), and the session stops: incomplete metadata never counts as verified.
  - Exit 4 means the release is not installed (or GOG's registry entry is unusable), and the session stops.
- **The pinned set** (D-008, amended 2026-10-27): base game + Expansion Pass + High Resolution Texture Pack, exe SHA-256 `F16DC949…` (F-ENV-012). The check passes on it, and the owner's launch test on it passed (F-ENV-013). The base exe's fingerprint (`8F84F4A6…`, F-ENV-009) stays recorded as history; it is not the target.
- **Nothing measured on another build carries over:** not the Steam build, not another GOG build, not third-party offsets (D-005). A new build means a new check and a new decision.

## 2. The question M1 answers

> Can **one** gameplay value that co-op needs be observed **reproducibly**, without changing anything in the game, its files, its saves or its process?

- **The value: the player character's world position** (three coordinates), because M2 (player transform) needs it first.
- **Out of M1:** every other variable (health, Nemesis, missions, AI). Each later milestone states its own.

## 3. Observation methods, in order (FB-30-02)

| Tier | Method | Allowed when |
|---|---|---|
| **A** | What the game exposes by itself: listing its own configuration and log files and reading them **without changing them**; for saves, only their **metadata** (name, size, time, SHA-256), never their contents | Once the owner approves M1 |
| **B** | External, strictly read-only observation of the running process: opening it with read-only access rights and reading memory, **no writing** | Only if tier A cannot answer the question, with a **separate owner approval** and a predeclared experiment that fixes its scope and stop conditions |

**If tier A cannot answer (FB-32-03):** when the permitted data do not contain the player position, M1 records the explicit outcome **Tier A INCONCLUSIVE / NO DATA**, with what was looked at. That outcome ends tier A. It never escalates to tier B by itself: tier B starts only after the owner's separate approval of a predeclared experiment.

**Never, at any tier:**
- writing to game memory, injecting a DLL, hooking, patching, or attaching a debugger that stops or changes the process;
- modifying, replacing or deleting any game file or save (rule 1);
- analysing, unpacking or dumping any protection (rule 2);
- any contact with WB online services or anti-cheat (rule 3);
- committing game files, extracted data, memory dumps, saves or their contents (rule 4);
- running third-party mod tools or their binaries (rule 8);
- compiling in third-party addresses or offsets (D-005).

## 4. Minimal evidence

Per session (`tools/m1/session.py` holds the record format and the outcome rules):
- the build check output (start and end), which covers the exe and every file in `x64`;
- a snapshot (size and SHA-256, files opened read-only) of every game-exposed folder used, saves included, before and after. **The game itself writes some of them**: it autosaves and appends to its logs. The M1 predeclaration lists those folders (`game_written`) before any session; a change anywhere else is an unexpected write, and a file removed anywhere is a FAIL. Our tools never open a game file for writing;
- the observed values, with timestamps, and for each one the file it came from (provenance);
- the scripted player actions (for example "stand still 0–5 s, walk north 10–15 s") and the tolerances, fixed in the predeclaration;
- how the game ended: its exit code (from the trace) and whether the player closed it through its menu;
- the network record (§4a).

**Before each session the owner copies the saves aside** (FB-34-03): an offline backup made by the owner, kept outside the repository, never committed or uploaded, and never read by our tools. The experiment tools never modify the live saves.

The evidence goes in `evidence/` as experiment records, with no local paths.

## 4a. Network monitoring (FB-33-02)

- **Tool:** Windows' own event tracing, nothing installed: `tools/m1/netwatch.py` drives `logman` and `tracerpt`. One trace session records three providers: `Microsoft-Windows-Kernel-Network` (every TCP and UDP event, IPv4 and IPv6), `Microsoft-Windows-DNS-Client` (every lookup) and `Microsoft-Windows-Kernel-Process` (every process start and exit, with image name, parent and exit code). Event tracing records every event, not samples, so brief connections are seen.
- **Per process:** each event carries its process ID. The targets are the processes whose image is `ShadowOfWar.exe`, plus their child processes (a crash reporter, for example).
- **Buffers (amended after EXP-048 session 1, KI-45):** the trace runs with explicit buffers: 256 KB each, 64 to 512 of them, so at most 128 MB (`netwatch.BUFFERS`). With logman's defaults, session 1 lost 1203 events. The sizes are measured before any session by EXP-050: the session buffers must lose nothing under the loopback flood that the tiny buffers lost events to. The record keeps the buffers used and the total event count.
- **Window:** the trace starts before the game is launched and stops after it has exited. Starting and stopping need an elevated prompt (an owner action, as for the dry run).
- **Coverage is proven, not assumed.** A canary process makes one loopback TCP connect, one loopback UDP send and one DNS lookup of a reserved name (`mordor-canary-start.invalid`, `mordor-canary-end.invalid`) right after the start and right before the stop. Coverage requires both canaries fully seen, no lost events, and the game's start **and** exit inside the trace. Anything less is `not_covered`.
- **Unexpected contact:** any non-loopback traffic by a target, or any DNS lookup by a target, even with no connection after it (FB-34-02). WB services are the main concern, but anything counts. Loopback traffic is recorded and allowed.
- **What a clean record means, and what it does not** (FB-34-02): it means no such event was **attributed to a target** while coverage was proven. It is not proof that no network activity happened. Traffic another process makes on the game's behalf (a system service, a launcher started before the trace) is not attributed to the game, and a mechanism the providers do not report would not be seen. EXP-046 measures which mechanisms are attributed (IPv6, a TCP attempt that never connects, a DNS lookup, very short flows, a child process, lost events); the record says what was proven, not more.
- **Recording:** the raw trace holds every process's traffic on the machine, so it stays under `%TEMP%` and is deleted after the analysis, on every path (FB-34-04). `--keep-raw` is for debugging only: a session record with a kept or undeletable raw file is a FAIL (condition 10). A new trace never starts beside an old raw file, and `netwatch.py cleanup` deletes one left by an interrupted run. The record keeps only, per target: event kinds, the peer as loopback/private/public, both ports of the event (which one is remote depends on its direction), the DNS names it looked up, and the counts. No addresses, no other processes.
- **Monitoring failure** (the trace does not start or cannot be decoded, a canary is missing, events are lost, the game is not fully inside the window, or a target's PID was reused during the trace, which makes attribution ambiguous): the session is INCONCLUSIVE and stops (condition 9), never PASS. Processes are tracked by their sequence number, which Windows never reuses.
- **A contact that was seen is a FAIL even when coverage is incomplete:** losing other events never hides one that was recorded.
- **Rule 3 is unchanged:** a contact with WB services is a stop; we never initiate one.

## 5. Privacy limits

- The evidence never contains:
  - the Windows user name, local paths, or account or platform identifiers;
  - save contents;
  - screenshots or video, unless the owner asks for them for a given session.
- Values are game-world numbers only.

## 6. Session outcomes, go/no-go and stop conditions (FB-30-03, FB-32-04, FB-33-04)

**Every session ends with exactly one outcome**, given by `classify` in `tools/m1/session.py` from the session record:

| Outcome | Meaning |
|---|---|
| **PASS** | Every requirement below holds, and the position behaved as the player's actions predict. |
| **FAIL** | Any of stop conditions 2–6, 8 and 10, a failing end build check, or a position that did not follow the actions while the evidence was otherwise sound. A FAIL condition wins over everything except NOT RUN. |
| **INCONCLUSIVE** | No FAIL condition, but the evidence cannot decide: no position in the permitted data (Tier A INCONCLUSIVE / NO DATA), ambiguous provenance (condition 7), too few observations per action, unreadable evidence, or network monitoring that did not cover the session (condition 9). |
| **NOT RUN** | The start build check did not exit 0, so the session never started. Nothing about the game is concluded from it. |

**Session requirements** (all needed for PASS):
- the build check exits 0 at the start **and** at the end (exe and every `x64` file unchanged);
- nothing removed from any snapshotted folder, and nothing changed outside the predeclared game-written folders;
- network monitoring covered the whole session and saw no unexpected contact (§4a);
- the observed position stays within the still tolerance while the player stands still and moves at least the movement minimum while the player walks, with at least the minimum number of observations per action and a clear provenance for each;
- the game exits with code 0, closed by the player through its menu.

**M1 overall:**
- **Go:** **3 separate PASS sessions**. This is the matrix's criterion, made specific. Any FAIL, INCONCLUSIVE or NOT RUN session does not count towards the three.
- **No-go:**
  - observation is only possible by circumventing a protection;
  - or only by writing to the process or the files;
  - or the observation itself destabilises the game.
- **Tier A INCONCLUSIVE / NO DATA** is neither go nor no-go: it is reported, and the next step is the owner's decision (§3).

**Stop at once, then record and report** if any of these happens:
1. the build check does not exit 0 (at the start: NOT RUN; at the end: FAIL), or the exe or any `x64` file changed;
2. anything suggests a protection or anti-tamper reaction (an integrity error, the game refusing to run, a debugger or tamper warning): FAIL;
3. any method would need writing, injection or circumvention: FAIL;
4. any unexpected online contact is seen, in particular with WB services: FAIL;
5. a file is removed, or a file outside the game-written folders changes: FAIL;
6. the game crashes or exits uncleanly: FAIL;
7. the provenance of an observed value is ambiguous (we cannot say which permitted file it came from): INCONCLUSIVE;
8. read-only operation cannot be confirmed for a method (then that method is not used): FAIL;
9. network monitoring did not cover the session, or the evidence could not be read: INCONCLUSIVE;
10. a raw trace was kept or could not be deleted (other processes' traffic left on disk): FAIL.
- **Two players:** the second player's copy gets the same read-only check (`check_gog_build.py` exit 0 on that machine) before any two-player test.

## 6a. Dry run before approval (FB-33-03)

`tools/m1/dryrun.py` (EXP-045) runs all of the above on fixtures in a temporary folder, with a stand-in process instead of the game: build checks, snapshots, a real trace, the log read and the outcome rules. Each of its 11 cases injects one fault and states its expected outcome first. Run elevated, the baseline must PASS and every fault must give its outcome; run without elevation, every case that needs the network must come out INCONCLUSIVE. It never touches the game, its files or its saves. The owner approves tier A only after it passes elevated, together with the coverage experiment EXP-046 (`tools/m1/coverage.py`, FB-34-01). Both passed elevated on 2026-10-27 (EXP-045 11/11, EXP-046 7/7; the owner's runs on `b500439`).

**After the buffer change (KI-45):** the owner runs both again elevated on the amended code, before EXP-051 session 1. `coverage.py` is EXP-050, and its nine cases add two that must both be a MATCH:
- `flood_with_session_buffers`: covered, 0 events lost, the flood attributed, and the stand-in's record that all 200000 sends completed (Codex pass 9).
- `mixed_load_with_session_buffers` (review #39, FB-39-03; added at the owner's request on 2026-10-28):
  - **The traffic:** one stand-in does every kind, one after the other. That is IPv6 loopback TCP and UDP, 50 brief loopback TCP connections, the 200000 flood, a DNS lookup, a public UDP send and a child process's public UDP send.
  - **The background, for that stand-in's whole life:** unrelated stand-ins, not targets, each send 400000 loopback datagrams. A finished one is replaced within 50 ms.
  - **The pass condition:** covered, 0 events lost, every kind attributed to the target or its child (the flood too), and every stand-in's record complete.
  - **Other outcomes:** a send refused through the stand-in's status is NOT_APPLICABLE. A stand-in that stops early, for example because a flood send was refused, is a MISMATCH (Codex pass 13).

`dryrun.py` must still give 11/11 with the session buffers. Both are fixture-only: they never touch the game.

**Both passed elevated on 2026-10-28** (EXP-050, the owner's runs on clean `f2ef9bd`):
- `coverage.py` 9/9 MATCH:
  - the flood: 0 of 200280 events lost;
  - the mixed load: 0 of 600792 events lost, every kind attributed;
  - the tiny buffers still lost 135455.
- `dryrun.py` 11/11.

**After the analysis change (KI-45, EXP-051 session 1):** the owner runs both again elevated on the amended `netwatch.py` before EXP-060 session 1. They must give the same outcomes: `coverage.py` 9/9 MATCH and `dryrun.py` 11/11. `test_m1.py` adds four checks:
- the analysis's memory does not grow with the number of events. The old code peaked at 133 MB on 30010 events; the new code stays under 8 MB, and peaked at 1 MB on 300009 events (a 143 MB file);
- an interrupt reaches the analysis, inside the parser and between events, and the raw trace is still deleted;
- only the root's own events count;
- a truncated file is still "no decodable trace".

**Result (2026-10-29, the owner's runs on clean `7126ff0`, code `f1665ae`):**
- `coverage.py` 9/9 MATCH, NO MISMATCH (`evidence/logs/m1-coverage-2026-10-29-063355-elevated`). The flood: 0 events lost of 200254, all 200000 sends attributed. The mixed load: 0 events lost of 600696, every kind attributed, the background stand-in complete. Forced loss, with the tiny buffers, lost 115996 events and was not covered, as required.
- `dryrun.py` 11/11 (`evidence/logs/m1-dryrun-2026-10-29-063519-elevated`). Its record says `dirty: true` only because the coverage run's folder was still untracked; no tracked file differed from `7126ff0`.
- Both are stand-ins on loopback, not the game.

## 7. Decisions needed from the owner

1. ~~Which exe D-008 pins~~: **decided 2026-10-27**, the installed set (`F16DC949…`); the launch test passed (F-ENV-013). The second player installs the same three GOG products.
2. ~~Approval of this document for M1 **tier A**, with the predeclaration in §8~~: **approved 2026-10-27** by the owner in chat ("Aprovo o §8 dos critérios da M1 para o nível A"), as of commit `6f2ed04` (code `e171fe0`). The owner runs session 1 after the KI-35 experiment on PR #25 has finished. Any later change to §8 or to `tools/m1/tier_a.py`, `session.py` or `netwatch.py` needs a new approval. The dry run and the coverage experiment (§6a) passed elevated, and ChatGPT reviewed them (review #35 and its follow-up).
   - Review #36 called §8 a sufficiently narrow draft, once its changes were made. They are made: the outcome tests, the privacy checks and a fixture-only rehearsal of the tool.
   - Review #37 found §8 ready for the owner's review, conditionally.
   - Codex pass 7 (2026-10-27) found 4 defects, all fixed with regressions: a UTF-16 file read as having no position, a `.sav.bak` vouching for a new id, an id embedded in a longer name, and an earlier killed session's raw trace. Codex pass 8 checked those fixes and found 1 defect, also fixed: a save id equal to the machine name still leaked inside a longer name.
3. ~~Approval of the amended §8 for EXP-051~~: **approved 2026-10-28** by the owner in chat ("Eu aprovo"), as of commit `fe0af56` (code `322f02d`). The approval came before ChatGPT's review of request #39, the owner's choice. The technical gate stays: **EXP-050 and the dry run must pass elevated on code `322f02d` before EXP-051 session 1.** If either fails, or anything in §8 or the session tools changes, the approval no longer covers it and a new one is needed. The amendment follows EXP-048 session 1 (KI-45), and it covers only:
   - the trace buffers (§4a);
   - the Windows-1252 fallback;
   - `user/gpu.nfo` while it is binary;
   - the experiment id;
   - two more counts in the record.

   Before the approval, the plan was: EXP-050 and the dry run elevated (§6a), then review, then the owner's approval. The owner approved first. `tier_a.py` must not run until EXP-050 and the dry run have passed elevated.
   - **2026-10-28, review #39 (FB-39-03):** the owner asked for EXP-050's mixed-load case and approved adding it in chat ("Sim, acrescenta"). It changes only `coverage.py`, its test and §6a.
     - The session tools (`tier_a.py`, `session.py`, `netwatch.py`) stay exactly as approved at `322f02d`.
     - "EXP-050 on code `322f02d`" therefore means those session tools, together with the `coverage.py` that holds the mixed-load case.
4. ~~Approval of the second amendment for EXP-060~~: **approved 2026-10-29, as of commit `c4e9cce`**, with the label fix below. The owner approved it on 2026-10-29 in chat ("Aprovo"), as of commit `a8f5d5a` (code `f1665ae`; since `322f02d` the only session tool changed was `netwatch.py`). Right after, before any run, `tier_a.py` was found to still label `session.json` "EXP-051". At the owner's choice its `EXPERIMENT` constant now says "EXP-060" (and its docstring). That is a change to a session tool, so the approval of `a8f5d5a` did not cover it. **The owner approved it on 2026-10-29 in chat ("Aprovo"), as of commit `c4e9cce`.** Any later change to §8 or the session tools needs a new approval. Nothing else changed, so EXP-050 and the dry run need not run again (neither runs `tier_a.py`). EXP-051 session 1 ran out of memory in the trace analysis and left no evidence (§8, KI-45). The fix streams the analysis. It changes only `netwatch.py`, and its result is unchanged. It needs:
   - ~~EXP-050 and the dry run elevated again, on the amended code~~: **passed 2026-10-29** (clean `7126ff0`, code `f1665ae`; 9/9 MATCH, 11/11; §6a);
   - ~~the owner's approval in chat~~: given 2026-10-29 for `a8f5d5a`, and again for the label change as of `c4e9cce`.

   EXP-060 session 1 may run. It has the same preparation as before: saves copied aside offline, GOG Galaxy and Steam closed, no KI-35 run at the same time.

## 8. Tier A predeclaration (EXP-048 approved by the owner on 2026-10-27; amended on 2026-10-28 for EXP-051, approved by the owner the same day; second amendment for EXP-060 approved by the owner on 2026-10-29)

Fixed before any session, after a metadata-only listing of the game's folders that the owner approved (F-ENV-014). No game-side run happens before the owner approves this section.

**The 2026-10-28 amendment** (KI-45; marked "amended" below) is the only change from the approved text. EXP-048 keeps its result: session 1 ran under the approved text and stays as it is. The amended series is **EXP-051**, and it starts again at session 1.

- **Folders (labels):**
  - `user`: `%LOCALAPPDATA%/WB Games/Shadow of War`. It holds the saves, render.cfg, gpu.nfo and CloudStorage, and is shared with the earlier Steam install. Files are hashed; saves are hashed but never read.
  - `install`: the GOG folder, found through GOG's registry entry. It is recorded by size and modification time, because its data is too large to hash twice; `x64` is hashed by the build check. The text files a session may read (the permitted extensions, up to 4 MB) are hashed at both ends (FB-36-03).
    - **Limit:** size and time are a low-assurance change signal. They establish nothing about the content of the other non-`x64` files. A file added or removed, or a size or time that changes, is still an unexpected write or a removal.
  - `Documents/WB Games` holds only another game (Shadow of Mordor) and is not used.
- **Permitted reads:**
  - files ending `.cfg .ini .log .txt .json .xml .nfo`, up to 4 MB, in both folders;
  - opened read-only;
  - never a `.sav`, never `x64`.
- **Game-written (`GAME_WRITTEN`):**
  - `user/*_ShadowOfWar.sav`, `user/render.cfg` and `user/gpu.nfo`, at that depth only;
  - anything else added or changed is an unexpected write: CloudStorage (new WB content), the install, a new file. The tool never writes to either folder.
  - The save pattern covers the saves of both ids in the folder (the earlier Steam install's and the GOG copy's). A save under an id that had no save before the session is an unexpected write (FB-36-01). The ids are compared in memory and never recorded.
    - Only whole save names count as having a save: a `<id>_ShadowOfWar.sav.bak` does not.
  - These patterns are fixed. They are never broadened after a session has been seen.
- **What counts as an observed position:** a line with a label (`pos`, `position`, `loc`, `location`), then three numbers, and a clock time `HH:MM:SS` on the same line.
  - Only lines that were **not** in the permitted files before the session count: an old log cannot pass.
  - Its time is relative to t=0.
  - A line without a clock is an invalid observation (INCONCLUSIVE, never PASS).
  - These count as unreadable evidence (INCONCLUSIVE, never PASS):
    - a permitted file that cannot be read, before or after the session;
    - a file over 4 MB;
    - a link, which is never followed;
    - a folder that cannot be listed.
  - A labelled line whose numbers cannot be read exactly (for example `1e6`) is an invalid observation, never a truncated one.
  - A permitted file is read as UTF-8, with or without a BOM, or as UTF-16 with a BOM. Any other encoding is unreadable evidence, never guessed. A UTF-16 file read as UTF-8 would hide its labels and look like a file with no position (Codex pass 7).
    - **Amended (KI-45):** a file with no BOM that is not valid UTF-8 is read as Windows-1252, as GOG's `EULA.txt` is. The file is listed as "read as Windows-1252".
      - A UTF-8 line inside such a file turns into other letters (`position—1` becomes `positionâ€”1`). The label can lose its word boundary, or the gap before its numbers can grow past 12 characters. So in a Windows-1252 read, **any line with a non-ASCII character that contains `pos` or `loc` anywhere is a candidate**. If it does not read exactly as a position, it is an invalid observation (INCONCLUSIVE), never silent no data. A Windows-1252 fraction (`1½`) is never read as a number (Codex passes 9 and 10).
      - A file read as Windows-1252 at one end of the session and not at the other is unreadable evidence: its old lines would no longer match the baseline and could look new (Codex pass 10).
      - Some files stay unreadable evidence:
        - a file with a NUL byte and no BOM (UTF-16 without a BOM, UTF-32, binary);
        - a file with one of the 5 byte values Windows-1252 leaves undefined;
        - a file whose BOM does not decode.
    - **Amended (KI-45):** `user/gpu.nfo` is game-written and was binary in session 1. When it cannot be read as text and contains a NUL byte, it is hashed by the snapshot and listed as "known binary: hashed, not searched", not counted as unreadable. Whenever it decodes as text, UTF-16 with a BOM included, it is searched like any other file (Codex pass 9). Any other binary file stays unreadable evidence.
  - **The limit of NO DATA** (Codex passes 10 and 11): NO DATA means that no new line matched the candidate rule. That rule is the ASCII word `pos`, `position`, `loc` or `location`, followed within 12 characters that are not digits, signs or decimal points (`+`, `-`, `.`) by a number (Codex pass 12); in a Windows-1252 read, also any non-ASCII line naming `pos` or `loc`. Several lines are not candidates:
    - a label with a longer gap before its numbers (`position (world coordinates) = 1 2 3`);
    - a label split by a soft hyphen or another invisible character;
    - a label in another language;
    - binary data.

    It does not mean the files hold no position in any form.
  - **Amended (Codex pass 11):** a file read one way at one end of the session and another way at the other is unreadable evidence. The ways are plain, Windows-1252 and known binary, so this includes `gpu.nfo` turning from binary to text.
  - Nothing else is interpreted.
- **Procedure:** `python -I tools/m1/tier_a.py --session N`, run by the owner from an elevated PowerShell.
  - **Before:**
    1. The owner copies the saves aside offline.
    2. GOG Galaxy, Steam and the game must be closed. The tool refuses otherwise.
    3. The build check must exit 0, or the session is NOT RUN.
    4. The tool takes the snapshots and starts the trace. If the trace does not start, the game is not launched.
  - **The run:**
    1. The owner launches `ShadowOfWar.exe` directly, loads a save and stands in an open place.
    2. The owner presses Enter in the console. t=0 is 10 s later.
    3. **One beep:** stand still, 0–30 s.
    4. **Two beeps:** walk forward without stopping, 30–60 s.
    5. **Three beeps:** done. The owner quits through the game's menu.
  - **After:**
    1. The tool asks whether the game was closed through its menu, showed any integrity, tamper or debugger warning, or crashed.
    2. It stops and analyzes the trace, which is then deleted.
    3. It re-runs the build check and the snapshots, reads the permitted files and classifies the session (§6).
  - **Second amendment (KI-45, EXP-051 session 1; approved by the owner on 2026-10-29, as of `a8f5d5a`, code `f1665ae`; the label fix in `tier_a.py` approved as of `c4e9cce`):** the analysis streams the decoded trace, one event at a time, and keeps only counts. It no longer loads the whole trace into memory, and Ctrl+C reaches it.
    - Its result is unchanged for any trace tracerpt writes.
    - **One difference:** a file that is both malformed and truncated reports the first fault it meets, for example `AttributeError` instead of `ParseError`. It is still "no decodable trace" (Codex pass 14).
    - The file is closed on every path, so it can still be deleted after an interrupt (Codex passes 14 and 15).
      - **Limit:** an interrupt inside the library's own call that opens the file, a few instructions long, can still leave the file open.
      - Then the raw file is reported as not deleted, which is a FAIL (condition 10), and the next session deletes it before it starts.
    - Only the root's own events count, as before.
  - Ctrl+C, or an error at any point after the trace is started, still stops the trace and deletes its raw files. The session ends INCONCLUSIVE, or FAIL if a FAIL condition was seen.
  - A game the trace never saw has no observed exit. That is missing evidence (INCONCLUSIVE), not an unclean exit.
  - A trace analysis that fails is recorded as monitoring that did not cover the session, with any raw file left (FB-36-05).
  - If the evidence itself cannot be written, the tool stops with an error. It names the raw files that were not deleted and the temporary folder it kept, and it deletes nothing more.
  - A session killed with its console runs none of this, and its trace keeps writing into that session's own temporary folder (Codex pass 7). So before each session the tool stops the trace and deletes any earlier session's temporary folder. If a raw file there cannot be deleted, it refuses to start.
  - The tool refuses to start if its evidence folder would sit inside the game's folders.
- **Tolerances:**
  - `still_max` 0.5;
  - `move_min` 5.0 (game units);
  - `min_obs` 3 per action.
- **Evidence:** `evidence/logs/m1-tier-a-<time>-s<N>/session.json`. It holds the outcome and reasons, the changes (not the full snapshots), the files scanned, the observations, the exit and the reduced network record. It has no local paths, no machine or user names and no save ids.
  - **Amended:** its `experiment` is EXP-051, and the network record also keeps `events_total` and `buffers`.
  - The redaction is case-insensitive.
  - It covers paths with either slash style or escaped backslashes, and any save id from the folder.
  - Ids are replaced wherever they appear: in another file's name, in an error message, in a plain string (FB-36-05). This includes inside a longer name (Codex pass 7). An id shorter than 4 characters is replaced only as a whole token. A machine or user name that is also a save id is replaced inside longer names too; otherwise names are replaced as whole tokens (Codex pass 8).
- **Expected (the hypothesis, not a result):** session 1 ends **Tier A INCONCLUSIVE / NO DATA**, because the game writes no log file (F-ENV-014). If so, and every other gate is sound, tier A ends after that one session (§3), and the owner decides whether a tier B predeclaration is written.
  - `session.json` records this as `no_data_established`. It is true only when the session is INCONCLUSIVE and no position is its only reason (with the zero counts per action that follow from it).
  - A session whose monitoring or evidence failed is INCONCLUSIVE too, but it cannot establish NO DATA, so it does not close tier A (FB-36-02).
  - Tier B is never started automatically. **Go** needs 3 PASS sessions. Any FAIL, or any other INCONCLUSIVE, stops the series.
- **Session 1 (2026-10-28, tool at `757ac6a`, clean; `evidence/logs/m1-tier-a-2026-10-28-113203-s1/`): INCONCLUSIVE, `no_data_established` false.**
  - **Held:**
    - the build check was 0 at both ends;
    - nothing was removed;
    - only GAME_WRITTEN files changed (three saves changed, a new slot under an existing id);
    - the exit code was 0, inside the trace, through the menu;
    - no protection warning and no crash were reported;
    - both canaries were seen;
    - no contact or DNS lookup was attributed to the game;
    - the raw trace was deleted.
  - **No position** in the 6 permitted files that were read.
  - **Not established:**
    - **1203 ETW events were lost**, so monitoring did not cover the session (condition 9). The trace uses logman's default buffers, and EXP-046 provoked loss only with tiny buffers.
    - **Two unreadable files:**
      - `install/EULA.txt` is Windows-1252 text: no BOM, no NUL, 100 bytes ≥ 0x80, not valid UTF-8;
      - `user/gpu.nfo` is binary: 134 of its 208 bytes are NUL.

      Both diagnoses come from byte statistics after the run; no content was recorded.
  - **The series is stopped** (predeclared). The fixes are the 2026-10-28 amendment above. They need EXP-050, review and the owner's new approval before EXP-051 session 1. The EXP-051 hypothesis is the same as EXP-048's: Tier A INCONCLUSIVE / NO DATA, with `no_data_established` true.
- **EXP-051 session 1 (2026-10-28, started 15:36 on clean `d872ecc`, session tools as at `322f02d`): no result, no evidence.**
  - **What happened:**
    - The owner played the session and answered the closing questions.
    - The trace was stopped and decoded.
    - The analysis then loaded the whole decoded trace into memory. At 18:20 it held 8.5 GB and was paging. Later it held 18 GB, and 537 MB of the machine's 32 GB were free.
    - Ctrl+C did not reach it, because one call parsed the whole file.
    - The owner ended the process after 19:46.
  - **What is left:**
    - No `session.json` was written, so the evidence folder `evidence/logs/m1-tier-a-2026-10-28-153601-s1/` is empty.
    - The session's temporary folder, with the raw trace, was no longer in `%TEMP%` when checked at about 19:47. Every other `mordor-*` folder in `%TEMP%` was gone too: the folder was cleared by something outside these tools, which was not identified.
    - The trace session `mordor-m1-net` was not running.
  - **Cause:** a defect in `netwatch` (KI-45). Its analysis was sized only on stand-in traces (EXP-045, EXP-046, EXP-050), never on a whole session's trace with the session buffers.
  - **The series is stopped:** monitoring and evidence failed, so the session counts as INCONCLUSIVE (§6, condition 9). EXP-051 is ABANDONED.
  - **The second amendment** (above, under the procedure) is the only fix. It changes only `netwatch.py`, and it needs:
    1. ~~EXP-050 and the dry run again, elevated, on the amended code (§6a)~~: passed 2026-10-29 on clean `7126ff0`;
    2. the owner's new approval: given 2026-10-29 ("Aprovo") for `a8f5d5a`; given again for `tier_a.py`'s label fix as of `c4e9cce` (§7 item 4).

    The next series is **EXP-060**, starting again at session 1. EXP-052 to EXP-059 are left to PR #25. Everything else in §8 is unchanged, including the hypothesis.
3. Later, and only if tier A cannot answer: a separate approval for **tier B**.
