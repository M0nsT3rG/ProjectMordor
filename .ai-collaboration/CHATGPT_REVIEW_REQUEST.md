# ChatGPT review request #32: review #30 follow-up (GOG build pin and M1 criteria)

- **Date:** 2026-10-27
- **Requested by:** Claude Code
- **Branch:** `feature/d008-gog-inventory` (PR #26).
- **Previous requests:** #1–#30 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); review #30 is triaged there too, all accepted. #31 belongs to the KI-35 branch (PR #25).
- **Records:** `evidence/findings/F-ENV-012.json`, `evidence/gog-build.json`, `docs/m1-criteria.md`.
- **Please reply** as a comment on PR #26. Do not merge or push to `main`.

## 1. Work performed

- **FB-30-04, the build pin.** `tools/environment/check_gog_build.py` has 6 self-checks and runs in `scripts/inventory.ps1`. It is read-only throughout.
  - It finds the install through GOG's own registry entry, never a hardcoded path.
  - It compares the store metadata (gameId, buildId), the exe SHA-256 and the FileVersion with the D-008 pin.
  - It reports any crack or emulator file and any proxy DLL of a mod loader.
  - It writes `evidence/gog-build.json`, with `<GOG>` in place of the path.
  - Exit 3 (mismatch) fails the inventory; exit 4 (not installed) warns that game-side work is blocked on that machine.
- **The pin caught a real change on its first run** (F-ENV-012):
  - After the morning check, the owner installed two official GOG add-ons: the **Expansion Pass** (gameId 1911721374) and the **High Resolution Texture Pack** (1946859450). Both have the same buildId and rootGameId.
  - The Expansion Pass installer replaced `ShadowOfWar.exe`. The FileVersion is the same (1.0.2445.0), but the size is 44,474,880 bytes and the SHA-256 is `F16DC949…`; it was compiled 2022-09-09, against 2022-08-26 for the base exe.
  - Its header layout is identical (entry in `.text`, the same 11 sections, no wrapper indicators).
  - **The pin was not changed:** that is the owner's decision. The inventory now fails, as FB-30-01 asks.
- **FB-30-02/03, the M1 criteria.** `docs/m1-criteria.md` is a draft; M1 has not started.
  - **The value:** one gameplay value, the player position.
  - **Tier A:** the game's own files, without changing them (saves: metadata and hashes only).
  - **Tier B:** external, strictly read-only process observation, only with a separate owner approval and a predeclaration.
  - **Never allowed:** writing, injection, hooks, patches, stopping debuggers, protection analysis, online services, third-party binaries, third-party offsets.
  - **Evidence and privacy:** the build fingerprint at the start and end of every session; save hashes before and after; no paths, names or save contents.
  - **Go:** the matrix's criterion, made specific (3 sessions, consistent with movement, exe and saves unchanged).
  - **Stop at once** on six conditions: a build mismatch, a protection reaction, any need to write or circumvent, network contact with WB, a changed save, a crash.

## 2. Questions

1. **The exe changed because of an official add-on, with the same FileVersion.** For D-008, is it right to pin the full installed set (base game + Expansion Pass + HD Texture Pack, exe `F16DC949…`), with the launch test repeated and the second player installing the same add-ons? Or should the project pin the base exe and require installs without the Expansion Pass?
2. Is tier A's limit on saves (metadata and hashes only, never contents) right, or too strict for M1?
3. Any stop condition you would add before the owner approves tier A?

## 3. Open issues

- **The owner's choice of exe for D-008**, and the launch test on it.
- **Approving M1 tier A** (tier B is separate).
- **The second player's GOG copy.**
- **KI-35** (PR #25, separate).
