# M1 criteria: read-only observation of game state (draft for the owner's decision)

- **Status: draft, 2026-10-27** (updated for review #32). Nothing in M1 has started. M1 begins only after the owner approves this document. Tier B (below) needs a second, separate approval.
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

Per session:
- the build check output (start and end), which covers the exe and every DLL in `x64`;
- the SHA-256 of every save file before and after;
- the list of game-exposed files read, each with its SHA-256 before and after (so that read-only operation is shown, not assumed);
- the observed values, with timestamps, and for each one the file it came from (provenance);
- a short description of what the player did (for example "walked north for 10 s");
- how the game ended (closed by the player through its menu, or otherwise);
- whether any network contact was seen, and how that was checked.

The evidence goes in `evidence/` as experiment records, with no local paths.

## 5. Privacy limits

- The evidence never contains:
  - the Windows user name, local paths, or account or platform identifiers;
  - save contents;
  - screenshots or video, unless the owner asks for them for a given session.
- Values are game-world numbers only.

## 6. Session outcomes, go/no-go and stop conditions (FB-30-03, FB-32-04)

**Every session ends with exactly one outcome**, recorded with its evidence:

| Outcome | Meaning |
|---|---|
| **PASS** | All the session requirements below hold, and the position was observed and behaved as the player's actions predict. |
| **FAIL** | The session ran and either broke a requirement below (stop conditions 1–6 and 8 do), or read a position that did not follow the player's actions (for example it did not change while walking). |
| **INCONCLUSIVE** | The session ran without breaking any requirement, but the evidence cannot decide: no position in the permitted data (Tier A INCONCLUSIVE / NO DATA), values whose provenance is ambiguous, or too few observations. |
| **NOT RUN** | The session did not start, for example because the start build check did not exit 0. Nothing about the game is concluded from it. |

**Session requirements** (all needed for PASS):
- the build check exits 0 at the start **and** at the end (exe and every `x64` file unchanged);
- every save hash, and every read game-exposed file's hash, is unchanged before and after;
- the observed position changes when the player moves and stays put when the player stands still, each value with a clear provenance;
- the game exits cleanly (closed by the player through its menu, with no crash);
- no unexpected online contact was seen.

**M1 overall:**
- **Go:** **3 separate PASS sessions**. This is the matrix's criterion, made specific. Any FAIL, INCONCLUSIVE or NOT RUN session does not count towards the three.
- **No-go:**
  - observation is only possible by circumventing a protection;
  - or only by writing to the process or the files;
  - or the observation itself destabilises the game.
- **Tier A INCONCLUSIVE / NO DATA** is neither go nor no-go: it is reported, and the next step is the owner's decision (§3).

**Stop at once, then record and report** if any of these happens. The session is FAIL, with two exceptions: a start build check that does not exit 0 means the session never started (**NOT RUN**), and condition 7 alone gives **INCONCLUSIVE**.
1. the build check does not exit 0 (at the start: NOT RUN; at the end: FAIL), or the exe or any DLL changed;
2. anything suggests a protection or anti-tamper reaction (an integrity error, the game refusing to run, a debugger or tamper warning);
3. any method would need writing, injection or circumvention;
4. any unexpected online contact is seen, in particular with WB services;
5. a save or game file changes, or any write we did not expect is seen;
6. the game crashes during observation;
7. the provenance of an observed value is ambiguous (we cannot say which permitted file it came from);
8. read-only operation cannot be confirmed for a method (then that method is not used).
- **Two players:** the second player's copy gets the same read-only check (`check_gog_build.py` exit 0 on that machine) before any two-player test.

## 7. Decisions needed from the owner

1. ~~Which exe D-008 pins~~: **decided 2026-10-27**, the installed set (`F16DC949…`); the launch test passed (F-ENV-013). The second player installs the same three GOG products.
2. Approval of this document for M1 **tier A**.
3. Later, and only if tier A cannot answer: a separate approval for **tier B**.
