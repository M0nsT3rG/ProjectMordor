# M1 criteria: read-only observation of game state (draft for the owner's decision)

- **Status: draft, 2026-10-27** (updated for reviews #32–#35; tier A predeclaration in §8). Nothing in M1 has started. M1 begins only after the owner approves this document. Tier B (below) needs a second, separate approval.
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

## 7. Decisions needed from the owner

1. ~~Which exe D-008 pins~~: **decided 2026-10-27**, the installed set (`F16DC949…`); the launch test passed (F-ENV-013). The second player installs the same three GOG products.
2. Approval of this document for M1 **tier A**, with the predeclaration in §8. The dry run and the coverage experiment (§6a) passed elevated, and ChatGPT reviewed them (review #35 and its follow-up).
   - Review #36 called §8 a sufficiently narrow draft, once its changes were made. They are made: the outcome tests, the privacy checks and a fixture-only rehearsal of the tool.
   - Before session 1, Codex pass 7 still runs on the final head (the owner deferred it).

## 8. Tier A predeclaration (EXP-048; awaiting the owner's approval)

Fixed before any session, after a metadata-only listing of the game's folders that the owner approved (F-ENV-014). No game-side run happens before the owner approves this section.

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
  - Ctrl+C, or an error at any point after the trace is started, still stops the trace and deletes its raw files. The session ends INCONCLUSIVE, or FAIL if a FAIL condition was seen.
  - A game the trace never saw has no observed exit. That is missing evidence (INCONCLUSIVE), not an unclean exit.
  - A trace analysis that fails is recorded as monitoring that did not cover the session, with any raw file left (FB-36-05).
  - If the evidence itself cannot be written, the tool stops with an error. It names the raw files that were not deleted and the temporary folder it kept, and it deletes nothing more.
  - The tool refuses to start if its evidence folder would sit inside the game's folders.
- **Tolerances:**
  - `still_max` 0.5;
  - `move_min` 5.0 (game units);
  - `min_obs` 3 per action.
- **Evidence:** `evidence/logs/m1-tier-a-<time>-s<N>/session.json`. It holds the outcome and reasons, the changes (not the full snapshots), the files scanned, the observations, the exit and the reduced network record. It has no local paths, no machine or user names and no save ids.
  - The redaction is case-insensitive.
  - It covers paths with either slash style or escaped backslashes, and any save id from the folder.
  - Ids are replaced wherever they appear: in another file's name, in an error message, in a plain string (FB-36-05).
- **Expected (the hypothesis, not a result):** session 1 ends **Tier A INCONCLUSIVE / NO DATA**, because the game writes no log file (F-ENV-014). If so, and every other gate is sound, tier A ends after that one session (§3), and the owner decides whether a tier B predeclaration is written.
  - `session.json` records this as `no_data_established`. It is true only when the session is INCONCLUSIVE and no position is its only reason (with the zero counts per action that follow from it).
  - A session whose monitoring or evidence failed is INCONCLUSIVE too, but it cannot establish NO DATA, so it does not close tier A (FB-36-02).
  - Tier B is never started automatically. **Go** needs 3 PASS sessions. Any FAIL, or any other INCONCLUSIVE, stops the series.
3. Later, and only if tier A cannot answer: a separate approval for **tier B**.
