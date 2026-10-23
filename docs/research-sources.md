# Research sources

Every finding cites its source with a commit hash in `evidence/findings/`. This page lists the sources reviewed and our trust posture.

All repositories were shallow-cloned into a temporary scratch directory **outside** this repository. They were read only: nothing was built or run, and none of their agent instructions were followed. No third-party code was copied into Project Mordor.

## Repositories

| Repository | Commit reviewed | License | Relevance | Findings |
|---|---|---|---|---|
| [Hagryph/SoWMods](https://github.com/Hagryph/SoWMods) | `ba0c3d9b090410d322d36a041155554c9682dc98` | **none** (all rights reserved) | `steam_api64.dll` proxy loader with MinHook and an ImGui overlay. It uses hardcoded RVAs with no build check, and its workflow relies on a Steamless-unpacked exe. | F-SOWM-001..018 |
| [cranebeaker/coop-anything](https://github.com/cranebeaker/coop-anything) | `5b86e5bb10ce864ab9872f2589651f73ffcae5db` | MIT | Co-op methodology and testing playbooks. Includes a `games/shadow-of-war/` write-up claiming our exact exe hash. | F-COOP-001..015 |
| [Shadow-of-War-Modding-Community/ImHex-Patterns](https://github.com/Shadow-of-War-Modding-Community/ImHex-Patterns) | `fcb03e2e6f6236370e0e2dae17f61fb96b5180e1` | none | Partial file-format patterns (LTAR, GADB, SKEL, ...) | F-COMM-002..005 |
| [Shadow-of-War-Modding-Community/MeshParser](https://github.com/Shadow-of-War-Modding-Community/MeshParser) | `e7821cd9b5ef13b44f6a9d75750259d5c0075069` | none | Mesh to FBX converter (bundles Assimp binaries) | F-COMM-006 |
| [Shadow-of-War-Modding-Community/Documentation](https://github.com/Shadow-of-War-Modding-Community/Documentation) | `04e60fb7e1cf756f6699bd14b15b771e9c1e6953` | none | One-line README only | F-COMM-001 |
| [ReaperAnon/Middle-Earth-Mod-Loader](https://github.com/ReaperAnon/Middle-Earth-Mod-Loader) | `a753f948e18295782e24ab4b6e2bfb59be1059e0` | none | `bink2w64.dll` proxy that pattern-scans the exe; ships a prebuilt DLL (do not run) | F-COMM-008 |
| [survivalizeed/ShadowOfWarPacketLoader](https://github.com/survivalizeed/ShadowOfWarPacketLoader) | `77e29087b3908d7526e8a0bfc28e995cb4b13490` | none | Resource replacement via a file-read hook | F-COMM-009 |
| [SystematicSkid/shadow-of-war-save-editor](https://github.com/SystematicSkid/shadow-of-war-save-editor) | `c5dca9ac4310cade49b409565bb298c522c3c6c4` | none | Save format and Nemesis ("Faction") hints | F-COMM-010, 011 |
| [SystematicSkid/shadow-of-war-save-converter](https://github.com/SystematicSkid/shadow-of-war-save-converter) | `e75e00def8b95381995cb1928dbfe68424962c86` | MIT | Save container format | F-COMM-010 |
| [SystematicSkid/shadow-of-war-server](https://github.com/SystematicSkid/shadow-of-war-server) | `11d80ca21582281fa18f067e8960b669726b2f83` | none | Re-implementation of the official online backend. **Out of bounds for us** (rule 5) | F-COMM-012 |
| [SystematicSkid/sow-freecam](https://github.com/SystematicSkid/sow-freecam) | `64f0f9e2893688cba4e6822ac89ea3fad472a187` | none | Camera hook, signature-based | F-COMM-019 |
| [JustAddMP/ShadowOfWar](https://github.com/JustAddMP/ShadowOfWar) | `d14c59bf33e82c15fccb6596ef77d136f27e8e4d` | none | README only ("planned") | F-COMM-020 |

**Do not download:** several recently created "Shadow of War Cheats/Trainer" repositories by unknown accounts look like malware lures (F-COMM-021).

## Web and primary sources

- Steam store page for app 356190: lists Denuvo Anti-tamper (checked 2026-10-23). F-COMM-014.
- PCGamingWiki, *Middle-earth: Shadow of War*: DRM per store; the GOG release is DRM-free. F-COMM-017.
- WB Games EULA for Shadow of War, sections 1(b)–1(h). F-COMM-015.
- Google Patents, US 10,926,179 B2 (Nemesis system). F-COMM-016.
- Reports of Monolith Productions' closure, 2025-02-25. F-COMM-018.
- Nexus Mods pages returned HTTP 403 to automated access and were not reviewed. Discord communities were not reviewed.

## Tooling documentation (for Phase 2 planning)

- Microsoft PE/COFF specification: https://learn.microsoft.com/windows/win32/debug/pe-format (basis of `tools/environment/pe_inventory.py`)
- Winsock2 reference: https://learn.microsoft.com/windows/win32/winsock/ (`SIO_UDP_CONNRESET`, non-blocking UDP)
- Ghidra: https://github.com/NationalSecurityAgency/ghidra (not installed; see decisions D-004)
- x64dbg: https://x64dbg.com (not installed; attaching a debugger to a Denuvo-protected process conflicts with rules 3 and 4)
- Sysinternals Process Monitor: https://learn.microsoft.com/sysinternals/downloads/procmon (candidate for passive file and registry observation in Phase 2, with approval)

## Trust posture

- **Build-dependent claims** (RVAs, offsets, structure layouts) from third parties are recorded as HYPOTHESIS until independently validated against SHA-256 `ED0D95CA…0BDAEF`. The SoWMods RVAs carry no build check at all (F-SOWM-005).
- Claims that third parties reached by **unpacking the protected executable** (Steamless, F-SOWM-004/009) are evidence we may cite. Their methods are not ones we may reproduce (rule 3).
- **License:** most Shadow of War prior art has no license. Read it as documentation; never copy its code.
