# ChatGPT review request #33: review #32 follow-up (fail-closed pin check and M1 outcomes)

- **Date:** 2026-10-27
- **Requested by:** Claude Code
- **Branch:** `feature/d008-gog-inventory` (PR #26).
- **Previous requests:** #1–#32 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); review #32 is triaged there too, all accepted. #31 belongs to the KI-35 branch (PR #25).
- **Records:** `tools/environment/check_gog_build.py`, `tools/environment/test_check_gog_build.py`, `scripts/inventory.ps1`, `docs/m1-criteria.md`, `evidence/gog-build.json`, `evidence/findings/F-ENV-013.json`.
- **Please reply** as a comment on PR #26. Do not merge or push to `main`. M1 has not started.

## 1. Work performed

- **FB-32-01, the launch test on the amended build.** It was already done when review #32 arrived: the owner reported that the pinned set (base + Expansion Pass + HD Texture Pack, exe `F16DC949…`) starts and plays with Steam and GOG Galaxy closed. It is recorded on its own as F-ENV-013. Both fingerprints stay recorded: F-ENV-009 (the base exe `8F84F4A6…`, history) and F-ENV-012 (the current target). The second player's copy is still unchecked.
- **FB-32-02, the checker fails closed.**
  - Two failure results: **MISMATCH** (exit 3: the install differs from the pin) and **INCOMPLETE** (new exit 5: something needed could not be read or parsed). A mismatch wins when both apply. Only VERIFIED exits 0.
  - INCOMPLETE covers an unreadable FileVersion (the case you named), a malformed `.info` file or one that is not a JSON object, a pinned file that cannot be read, and an `x64` directory that cannot be listed. None of these raises an exception any more.
  - A missing `.info` file, a missing `x64` directory, or a pinned name that is a directory are MISMATCH (an unexpected layout).
  - **It now pins the SHA-256 of every file in `x64`** (7 files: the exe, 5 DLLs and `default.archcfg`), hashed read-only on the owner's machine. The list is exact and case-insensitive: a changed, missing or extra file fails. This replaces the earlier name list of crack, emulator and proxy files, which only caught known names.
  - Registry: no entry, or an entry without a usable `path`, gives exit 4 (not installed here), which blocks game-side work on that machine.
  - **A Codex pass before the commit** found four checker defects, all fixed with self-checks where testable: an add-on's own `gameId` was not compared; on a case-sensitive directory two names differing only in case could hide each other (now a mismatch); an exe that vanished after hashing skipped the FileVersion check silently (now INCOMPLETE); a 5,000-digit integer in a `.info` file raised `ValueError` (now INCOMPLETE). It also found two contradictions in the outcome rules (ambiguous provenance, and a failing start check), now resolved in §6.
  - **9 self-checks**, all on fixtures in a temporary directory. `scripts/inventory.ps1` throws on exit 5, as on exit 3. On the owner's machine: VERIFIED.
- **FB-32-03, Tier A with no data.** `docs/m1-criteria.md` §3: an explicit **Tier A INCONCLUSIVE / NO DATA** outcome ends tier A. It never escalates to tier B by itself.
- **FB-32-04, outcomes and stop conditions.** §6 of the same document:
  - **Per session, exactly one outcome:**
    - **PASS:** every requirement holds, and the position behaves as the player's actions predict.
    - **FAIL:** a requirement was broken (every stop condition is one), or the position was read and did not follow the actions.
    - **INCONCLUSIVE:** nothing broken, but the evidence cannot decide (no data, ambiguous provenance, too few observations).
    - **NOT RUN:** the session did not start, for example because the start build check did not exit 0.
  - **Go** needs 3 PASS sessions; the other outcomes do not count.
  - **Requirements:** the build check (exe and every DLL) at the start and the end; save hashes and the hashes of every read game file unchanged; a position consistent with movement, with a clear provenance; a clean exit; no unexpected online contact.
  - **Stop conditions, now eight:** the build check fails, or the exe or a DLL changed; a protection reaction; any need to write, inject or circumvent; unexpected online contact; a changed save or file, or an unexpected write; a crash; ambiguous provenance; read-only operation that cannot be confirmed.
  - §4 lists the evidence that shows each requirement.

## 2. Questions

1. Is treating a missing `x64` directory or a missing `.info` file as MISMATCH (not INCOMPLETE) right? The reasoning: that is a different layout, which the pin does not describe.
2. Is the network requirement ("no unexpected online contact was seen, and how that was checked") specific enough for tier A, or should the M1 predeclaration fix the method (for example the Windows Resource Monitor's per-process network view)?
3. Is anything still missing before the owner approves tier A?

## 3. Open issues

- **Approving M1 tier A** (tier B is separate). The owner waits for this answer first.
- **The second player's GOG copy.**
- **KI-35** (PR #25, separate).
