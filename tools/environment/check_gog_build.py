"""Read-only check that the installed GOG release is EXACTLY the set pinned by D-008 (FB-30-01/04, FB-32-02).

Finds the install through GOG's own registry entry (never a hardcoded path), then compares the store metadata of the
base game and its pinned add-ons, and the SHA-256 of EVERY file in x64 (an exact list: a changed, missing or extra
file, a crack or a mod-loader DLL all fail), plus the executable's FileVersion. Never writes into the game directory.

Fail-closed (FB-32-02): it never reports a verified build from incomplete metadata. Anything it cannot read or parse
is INCOMPLETE, not a pass.

Writes evidence/gog-build.json with the install path replaced by <GOG>.
Exit codes: 0 VERIFIED; 3 MISMATCH and 5 INCOMPLETE (both block game-side work until the build is re-checked and the
owner decides); 4 NOT_INSTALLED (game-side work is blocked on this machine).
  python -I tools/environment/check_gog_build.py [--repo <project root>] [--path <install dir>] [--no-write]
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from discover_game import sha256, version_info  # noqa: E402

# Pinned by D-008 as amended by the owner (2026-10-27): the base game + Expansion Pass + HD Texture Pack, whose
# installer replaced the exe (F-ENV-012). A change here is a new decision, never an update to make a check pass.
PIN = {
    "game_id": "1324471032", "build_id": "55823353107546309", "file_version": "1.0.2445.0",
    "addons": {"1911721374": "Expansion Pass", "1946859450": "High Resolution Texture Pack"},
    # Every file in x64, exactly (name -> SHA-256). The exe is the amended pin; the others are unchanged since F-ENV-009.
    "x64": {
        "ShadowOfWar.exe": "F16DC9492E0F31565071936A652252C80CA32EB6E9C07C331AE89BDD9C3DE5D8",
        "amd_ags_x64.dll": "2B4059AFFFA60078014FE490EB0700716689B55AB3A334435C69AF4292B985C1",
        "AnselSDK64.dll": "9887E9F96EA9FC1B216ADBB6E9362C25F2486AB4729AD3F0A780DC3C24B8414E",
        "bink2w64.dll": "A639F44E46828D58E9D14C097EE3B19A5AB416E6A0EED65749C7482C6E4B8D11",
        "default.archcfg": "070E3E792B4767A51A9DDB360EA54FB1DC7AE9B660D0D2174D78BD0B46272059",
        "oo2core_5_win64.dll": "4C3DCFEBB2BB59650690FEB4C63D68A5838C37C0FF233C2445269FEF0C5A40D0",
        "steam_api64.dll": "F12CF470954D9D956F7D44F9EB84659890C5722A021548AB88710340C0965230",
    },
}
EXE = "ShadowOfWar.exe"


def registry_path() -> tuple[Path | None, str]:
    """GOG's own entry for the game: (path, '') or (None, why)."""
    try:
        import winreg
    except ImportError:
        return None, "no Windows registry"
    key = rf"SOFTWARE\WOW6432Node\GOG.com\Games\{PIN['game_id']}"
    for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
        try:
            with winreg.OpenKey(hive, key) as k:
                value = winreg.QueryValueEx(k, "path")[0]
        except OSError:
            continue
        if not isinstance(value, str) or not value.strip():
            return None, "GOG registry entry has no usable 'path'"
        return Path(value), ""
    return None, "no GOG registry entry for the game"


def read_info(path: Path) -> tuple[dict | None, str]:
    """A goggame-<id>.info file: (metadata, '') or (None, why). Never raises."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return None, "missing"
    except (OSError, UnicodeDecodeError) as e:
        return None, f"unreadable ({type(e).__name__})"
    except (ValueError, RecursionError):  # JSONDecodeError, an over-long integer, absurd nesting
        return None, "malformed JSON"
    if not isinstance(data, dict):
        return None, "not a JSON object"
    return data, ""


def check(install: Path, pin: dict = PIN, file_version=version_info) -> dict:
    """The comparison itself (testable on any directory). `mismatches`: the install differs from the pin.
    `incomplete`: something needed could not be read, so nothing can be claimed. Verified only if both are empty."""
    mismatch, incomplete, facts = [], [], {}
    info, why = read_info(install / f"goggame-{pin['game_id']}.info")
    if info is None:
        (mismatch if why == "missing" else incomplete).append(f"base game store metadata {why}")
    else:
        facts["game_id"], facts["build_id"] = info.get("gameId"), info.get("buildId")
        if info.get("gameId") != pin["game_id"]:
            mismatch.append(f"gameId {info.get('gameId')} != {pin['game_id']}")
        if info.get("buildId") != pin["build_id"]:
            mismatch.append(f"buildId {info.get('buildId')} != {pin['build_id']}")
    for addon_id, name in pin.get("addons", {}).items():
        ameta, why = read_info(install / f"goggame-{addon_id}.info")
        if ameta is None:
            (mismatch if why == "missing" else incomplete).append(f"pinned add-on {name}: store metadata {why}")
        elif (ameta.get("gameId") != addon_id or ameta.get("buildId") != pin["build_id"]
              or ameta.get("rootGameId") != pin["game_id"]):
            mismatch.append(f"add-on {name}: gameId {ameta.get('gameId')} / buildId {ameta.get('buildId')} / "
                            f"rootGameId {ameta.get('rootGameId')} not pinned")
    x64 = install / "x64"
    if not x64.is_dir():
        mismatch.append("no x64 directory (unexpected layout)")
        return {"mismatch": mismatch, "incomplete": incomplete, "facts": facts}
    try:
        present = list(x64.iterdir())
    except OSError as e:
        incomplete.append(f"x64 not listable ({type(e).__name__})")
        return {"mismatch": mismatch, "incomplete": incomplete, "facts": facts}
    want = {n.lower(): (n, h) for n, h in pin["x64"].items()}
    have = {}
    for p in present:
        if p.name.lower() in have:  # a case-sensitive directory: never let one name hide another
            mismatch.append(f"two files in x64 differ only in case: {have[p.name.lower()].name}, {p.name}")
        have[p.name.lower()] = p
    facts["x64_files"] = sorted(p.name for p in present)
    for extra in sorted(set(have) - set(want)):
        mismatch.append(f"unexpected file in x64: {have[extra].name}")  # a crack, an emulator, a proxy DLL, a leftover
    for key, (name, pinned) in sorted(want.items()):
        p = have.get(key)
        if p is None:
            mismatch.append(f"pinned file missing: {name}")
            continue
        if not p.is_file():
            mismatch.append(f"{name} is not a regular file")
            continue
        try:
            got, size = sha256(p), p.stat().st_size
        except OSError as e:
            incomplete.append(f"{name} unreadable ({type(e).__name__})")
            continue
        if name == EXE:
            facts["sha256"], facts["size"] = got, size
        if got != pinned:
            mismatch.append(f"{name} SHA-256 {got} != pinned {pinned}")
    exe = have.get(EXE.lower())
    if exe is None or not exe.is_file():
        incomplete.append("FileVersion not checked: the exe is not a regular file")  # also if it vanished mid-check
    else:
        try:
            fv = file_version(exe).get("file_version_fixed")
        except OSError as e:
            fv = None
            incomplete.append(f"FileVersion unreadable ({type(e).__name__})")
        facts["file_version"] = fv
        if fv is None:
            if not any("FileVersion" in x for x in incomplete):
                incomplete.append("FileVersion not readable: version metadata missing (incomplete evidence)")
        elif fv != pin["file_version"]:
            mismatch.append(f"exe FileVersion {fv} != {pin['file_version']}")
    return {"mismatch": mismatch, "incomplete": incomplete, "facts": facts}


def verdict(r: dict) -> tuple[str, int]:
    if r["mismatch"]:
        return "MISMATCH", 3
    if r["incomplete"]:
        return "INCOMPLETE", 5
    return "VERIFIED", 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    ap.add_argument("--path", type=Path, help="install directory (default: GOG's registry entry)")
    ap.add_argument("--no-write", action="store_true")
    a = ap.parse_args(argv)
    install, why = (a.path, "") if a.path else registry_path()
    if install is None or not install.is_dir():
        print(f"GOG release not installed on this machine ({why or 'path is not a directory'}): game-side work is "
              "blocked here (D-008).")
        return 4
    r = check(install, PIN, version_info)  # module globals at call time, so the self-checks can substitute them
    result, code = verdict(r)
    out = {"checked_at": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"), "install": "<GOG>",
           "pinned": PIN, "result": result, **r}
    if not a.no_write:
        (a.repo / "evidence" / "gog-build.json").write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    if code:
        print(f"GOG BUILD {result}: game-side work is blocked until this build is re-checked read-only and the owner "
              "records a decision (D-008). Never reuse earlier offsets or assumptions.")
        for p in r["mismatch"] + r["incomplete"]:
            print(f"  - {p}")
        return code
    print(f"GOG build VERIFIED: {PIN['file_version']} exe sha256 {PIN['x64'][EXE][:12]}..., "
          f"{len(PIN['x64'])} x64 files and {len(PIN['addons'])} add-ons as pinned (buildId {PIN['build_id']})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
