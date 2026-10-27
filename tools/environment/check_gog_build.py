"""Read-only check that the installed GOG release is EXACTLY the build pinned by D-008 (FB-30-01/04).

Finds the install through GOG's own registry entry (never a hardcoded path), then compares the store metadata, the
executable's SHA-256 and FileVersion with the pinned values, and looks for files that do not belong to the release
(crack or emulator files, proxy DLLs of a mod loader). Never writes into the game directory.

Writes evidence/gog-build.json with the install path replaced by <GOG>.
Exit codes: 0 the pinned build; 3 MISMATCH (game-side work is blocked until it is re-checked and a new decision is
recorded); 4 not installed (game-side work is blocked on this machine).
  python -I tools/environment/check_gog_build.py [--repo <project root>] [--path <install dir>] [--no-write]
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from discover_game import PROXY_DLLS, sha256, version_info  # noqa: E402

# Pinned by D-008 as amended by the owner (2026-10-27): the base game + Expansion Pass + HD Texture Pack, whose
# installer replaced the exe (F-ENV-012). A change here is a new decision, never an update to make a check pass.
PIN = {"game_id": "1324471032", "build_id": "55823353107546309", "file_version": "1.0.2445.0",
       "sha256": "F16DC9492E0F31565071936A652252C80CA32EB6E9C07C331AE89BDD9C3DE5D8",
       "addons": {"1911721374": "Expansion Pass", "1946859450": "High Resolution Texture Pack"}}
# Files that are not part of the GOG release: Steam emulators and cracks (F-ENV-009's rejected-copy lesson).
FOREIGN = ["steam_emu.ini", "steam_settings", "steam_api64.cdx", "codex64.dll", "steam_appid.txt", "steamclient64.dll",
           "c000005.dll", "cream_api.ini", "SmartSteamEmu.ini", "goldberg_emulator", "OnlineFix.ini"]


def registry_path() -> Path | None:
    try:
        import winreg
    except ImportError:
        return None
    key = rf"SOFTWARE\WOW6432Node\GOG.com\Games\{PIN['game_id']}"
    for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
        try:
            with winreg.OpenKey(hive, key) as k:
                return Path(winreg.QueryValueEx(k, "path")[0])
        except OSError:
            continue
    return None


def check(install: Path, pin: dict = PIN) -> dict:
    """The comparison itself (testable on any directory): a list of mismatches, empty for the pinned build."""
    problems, facts = [], {}
    info = install / f"goggame-{pin['game_id']}.info"
    if not info.is_file():
        problems.append("no GOG store metadata (goggame-<id>.info)")
    else:
        meta = json.loads(info.read_text(encoding="utf-8"))
        facts["game_id"], facts["build_id"] = meta.get("gameId"), meta.get("buildId")
        if meta.get("gameId") != pin["game_id"]:
            problems.append(f"gameId {meta.get('gameId')} != {pin['game_id']}")
        if meta.get("buildId") != pin["build_id"]:
            problems.append(f"buildId {meta.get('buildId')} != {pin['build_id']}")
    for addon_id, name in pin.get("addons", {}).items():  # part of the pinned set (D-008 amendment)
        ainfo = install / f"goggame-{addon_id}.info"
        if not ainfo.is_file():
            problems.append(f"pinned add-on missing: {name} (goggame-{addon_id}.info)")
            continue
        ameta = json.loads(ainfo.read_text(encoding="utf-8"))
        if ameta.get("buildId") != pin["build_id"] or ameta.get("rootGameId") != pin["game_id"]:
            problems.append(f"add-on {name}: buildId {ameta.get('buildId')} / rootGameId {ameta.get('rootGameId')} not pinned")
    exe = install / "x64" / "ShadowOfWar.exe"
    if not exe.is_file():
        problems.append("no x64/ShadowOfWar.exe")
    else:
        facts["sha256"] = sha256(exe)
        facts["size"] = exe.stat().st_size
        facts["file_version"] = version_info(exe).get("file_version_fixed")
        if facts["sha256"] != pin["sha256"]:
            problems.append(f"exe SHA-256 {facts['sha256']} != pinned {pin['sha256']}")
        if facts["file_version"] not in (None, pin["file_version"]):  # None off Windows: the hash decides
            problems.append(f"exe FileVersion {facts['file_version']} != {pin['file_version']}")
    x64 = install / "x64"
    names = {p.name.lower() for p in x64.iterdir()} if x64.is_dir() else set()
    found = sorted(n for n in names if n in {f.lower() for f in FOREIGN})
    proxies = sorted(n for n in names if n in {d.lower() for d in PROXY_DLLS})
    if found:
        problems.append(f"files that are not part of the release: {found}")
    if proxies:
        problems.append(f"proxy DLLs (a mod loader?): {proxies}")
    facts["x64_files"] = sorted(names)
    return {"problems": problems, "facts": facts}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    ap.add_argument("--path", type=Path, help="install directory (default: GOG's registry entry)")
    ap.add_argument("--no-write", action="store_true")
    a = ap.parse_args(argv)
    install = a.path or registry_path()
    if install is None or not install.is_dir():
        print("GOG release not installed on this machine: game-side work is blocked here (D-008).")
        return 4
    r = check(install)
    out = {"checked_at": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"), "install": "<GOG>",
           "pinned": PIN, "result": "pinned build" if not r["problems"] else "MISMATCH", **r}
    if not a.no_write:
        (a.repo / "evidence" / "gog-build.json").write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    if r["problems"]:
        print("GOG BUILD MISMATCH: game-side work is blocked until this build is re-checked read-only and the owner "
              "records a new decision (D-008). Never reuse earlier offsets or assumptions.")
        for p in r["problems"]:
            print(f"  - {p}")
        return 3
    print(f"GOG build OK: {PIN['file_version']} sha256 {PIN['sha256'][:12]}... buildId {PIN['build_id']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
