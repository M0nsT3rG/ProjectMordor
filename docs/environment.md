# Environment report (sanitized)

Generated 2026-10-23 by read-only inspection. Machine-readable evidence: [`evidence/game-build.json`](../evidence/game-build.json), [`evidence/pe-inventory.json`](../evidence/pe-inventory.json). To regenerate: `./scripts/inventory.ps1`.

Absolute paths are replaced by `<STEAM>`, `<LIBRARY>` and `<GAME>`. Nothing was written to the game directory or the saves.

## Host system

| Item | Value |
|---|---|
| OS | Windows 11 Pro 10.0.26300, AMD64 |
| RAM | 31.9 GB |
| Free disk space | 5 fixed volumes, about 14 GB to 339 GB free each; the game volume has about 155 GB free |
| Python | 3.15.1 |
| C++ | MSVC 14.51.36231 (Visual Studio 18 Community, `vcvars64.bat` present) |
| CMake | 4.4.4 (Visual Studio 18 generator) |
| Git | 2.56.0 |
| Node.js | present (used only by the Codex CLI) |
| Not installed | Ghidra, x64dbg, Sysinternals (`sigcheck`, Process Monitor), `dumpbin` on PATH, Ninja, GitHub CLI |

Missing RE tools were **not** installed: installation needs user approval (rule 9), and Phase 1 found no lawful target for them yet (see [decisions.md](decisions.md) D-004).

## Steam and game discovery

- Steam root comes from `HKCU\Software\Valve\Steam\SteamPath`; `libraryfolders.vdf` lists 3 libraries.
- `appmanifest_356190.acf` was found in library index 2.

| Item | Value |
|---|---|
| App | 356190, *Middle-earth: Shadow of War* |
| Steam buildid / TargetBuildID | 3095001 / 3095001 |
| Install size (manifest) | 117,711,336,590 bytes |
| Executable | `<GAME>/x64/ShadowOfWar.exe`, 117,732,784 bytes |
| SHA-256 | `ED0D95CABB033CD66AAA99D699374881846C3CB03EC345F1123D82D45E0BDAEF` |
| VS_VERSIONINFO | FileVersion and ProductVersion 1.0.8874.0, WB Games, Inc., copyright 2018 |
| COFF timestamp | 1535072130 (2018-08-24 UTC) |
| PDB reference | `ShadowOfWar.pdb` (RSDS, age 3); the PDB is not shipped |

Files in `x64/`: `amd_ags_x64.dll`, `AnselSDK64.dll`, `bink2w64.dll`, `oo2core_5_win64.dll` (Oodle), `steam_api64.dll`, `default.archcfg`, `steam_appid.txt` (`356190`). Hashes are in `game-build.json`.

The game data is 71 `*.arch06` archives in `<GAME>/`: per-region archives, `Patch_00`–`Patch_13`, `Patch_DLC1`–`Patch_DLC5` with `_LC` variants, and per-language presentations.

`<GAME>/game/projectconsole.cfg` contains the single line `GameClientShell.LoadUnprefetchedResources 0`.

**No proxy DLL is present** (none of dinput8, version, winmm, xinput, dxgi, d3d11, dsound or winhttp), so no mod loader is installed. See F-ENV-002.

## PE inventory highlights (F-ENV-003..006)

- PE32+, AMD64, WINDOWS_GUI. DllCharacteristics: HIGH_ENTROPY_VA, NX_COMPAT, TERMINAL_SERVER_AWARE. **DYNAMIC_BASE (ASLR) is not set**, so the preferred image base is `0x140000000`. Authenticode is present. TLS directory with 1 callback.
- 17 sections with atypical names and flags (`.arch`, `.xtext`, `.bss a`, `.srdata`, `.bind`, ...). **The entry point is in `.bind`** (entropy 7.95). That is consistent with the SteamStub wrapper, and third-party sources also report Denuvo (F-SOWM-004, F-COMM-014, F-COMM-017).
- Static imports: 21 DLLs, 199 functions. No `ws2_32.dll`. HTTP comes from `wininet` and `winhttp`, plus `steam_api64`, `bink2w64` and the CRT. Delay imports: `d3d11.dll` and `amd_ags_x64.dll`.
- 286 exports: Wwise (`AK::SoundEngine`, `AK::StreamMgr`, `AK::MotionEngine`) and Scaleform symbols.

## Saves

`%LOCALAPPDATA%/WB Games/Shadow of War` exists. Its contents were **deliberately not listed or read**. Third-party sources say saves are `<uid>_ShadowOfWar.sav` there (F-COMM-010) and that Steam Remote Storage may mirror them to Steam userdata (F-COOP-013). Phase 2 must take a hash-manifest backup of both locations before any experiment that launches the game.

## Network environment notes

- UDP port 47800 is already bound on this machine by an unrelated application. Prototype runs therefore use explicit `--port` values (47811 and others).
- The prototype binds 127.0.0.1 by default. No firewall rule was created.

## Uncertainties

- buildid identifies the Steam depot build, not the engine version. The exe hash is the authoritative identifier, so re-run the inventory if Steam updates the game. Third-party sources say Monolith was closed in 2025, which makes further patches unlikely (F-COMM-018).
- The protector identification is a hypothesis drawn from section layout plus third-party reports. It was not analysed further (EXP-003, rule 3).
- The proxy-DLL check only covers the listed names.
