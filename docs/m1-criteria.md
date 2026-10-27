# M1 criteria: read-only observation of game state (draft for the owner's decision)

- **Status: draft, 2026-10-27.** Nothing in M1 has started. M1 begins only after the owner approves this document. Tier B (below) needs a second, separate approval.
- **Sources:** review #30 (FB-30-01..04); the M1 row of [feasibility-matrix.md](feasibility-matrix.md); D-007 and D-008 in [decisions.md](decisions.md).
- **Every non-negotiable rule in `/CLAUDE.md` applies.** Nothing here relaxes them.

## 1. Build fingerprint (FB-30-01, FB-30-04)

- **Only the build pinned by D-008.** `tools/environment/check_gog_build.py` (run by `scripts/inventory.ps1`) must exit 0 at the **start and the end** of every M1 session.
  - Exit 3 means a different build or foreign files, and the session stops.
  - Exit 4 means the release is not installed, and the session stops.
- **Today the check fails.** The installed exe changed when the official Expansion Pass was installed (F-ENV-012: SHA-256 `F16DC949…`, against the pinned `8F84F4A6…`). M1 cannot start until the owner decides which exe D-008 pins and the launch test is repeated on it.
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
- the build check output (start and end);
- the SHA-256 of every save file before and after;
- the observed values, with timestamps;
- a short description of what the player did (for example "walked north for 10 s").

The evidence goes in `evidence/` as experiment records, with no local paths.

## 5. Privacy limits

- The evidence never contains:
  - the Windows user name, local paths, or account or platform identifiers;
  - save contents;
  - screenshots or video, unless the owner asks for them for a given session.
- Values are game-world numbers only.

## 6. Success, failure and stop conditions (FB-30-03)

- **Go (success):** the player position is observed reproducibly in **3 separate sessions**, consistently with what the player does (moving changes it as expected; standing still keeps it). In each session the exe hash and every save hash are unchanged before and after. This is the matrix's criterion, made specific.
- **No-go (failure):**
  - observation is only possible by circumventing a protection;
  - or only by writing to the process or the files;
  - or the observation itself destabilises the game.
- **Stop at once, then record and report** if any of these happens:
  1. the build check fails;
  2. anything suggests a protection or anti-tamper reaction (an integrity error, the game refusing to run, a debugger or tamper warning);
  3. any method would need writing, injection or circumvention;
  4. any network contact with WB services is triggered by our work;
  5. a save or game file changes;
  6. the game crashes during observation.
- **Two players:** the second player's copy gets the same read-only check (`check_gog_build.py` exit 0 on that machine) before any two-player test.

## 7. Decisions needed from the owner

1. **Which exe D-008 pins.** Either the base exe (`8F84F4A6…`, which would mean reinstalling without the Expansion Pass), or the exe as now installed (`F16DC949…`, base game + Expansion Pass + HD Texture Pack, with the launch test repeated). In the second case the second player installs the same add-ons.
2. Approval of this document for M1 **tier A**.
3. Later, and only if tier A cannot answer: a separate approval for **tier B**.
