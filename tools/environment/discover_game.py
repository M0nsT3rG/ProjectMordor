"""Discover the Steam install of Middle-earth: Shadow of War (app 356190) read-only.

Writes sanitized evidence:
  evidence/game-build.json   - manifest build, exe hash/version, DLL hashes, proxy-DLL check
  evidence/pe-inventory.json - PE inventory of ShadowOfWar.exe and bundled DLLs

Never writes into the game directory. Absolute paths are replaced by <STEAM>,
<LIBRARY> and <GAME> placeholders before anything is saved.

Usage: python -I tools/environment/discover_game.py [--repo <project root>]
"""
from __future__ import annotations

import argparse
import ctypes
import datetime as dt
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_inventory import inventory, PEError  # noqa: E402

APP_ID = "356190"
# DLL names commonly abused as proxy loaders; their presence in x64/ would mean a mod loader is installed.
PROXY_DLLS = ["dinput8.dll", "version.dll", "winmm.dll", "xinput1_3.dll", "xinput1_4.dll", "dxgi.dll",
              "d3d11.dll", "dsound.dll", "winhttp.dll", "bink2w64_original.dll"]


def steam_root() -> Path | None:
    try:
        import winreg
        for hive, key in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
                          (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam")):
            try:
                with winreg.OpenKey(hive, key) as k:
                    for val in ("SteamPath", "InstallPath"):
                        try:
                            return Path(winreg.QueryValueEx(k, val)[0])
                        except OSError:
                            pass
            except OSError:
                pass
    except ImportError:
        pass
    return None


def vdf_values(text: str, key: str) -> list[str]:
    return [m.replace("\\\\", "\\") for m in re.findall(rf'"{key}"\s+"([^"]*)"', text)]


def libraries(steam: Path) -> list[Path]:
    libs = [steam]
    vdf = steam / "steamapps" / "libraryfolders.vdf"
    if vdf.is_file():
        libs += [Path(p) for p in vdf_values(vdf.read_text(encoding="utf-8", errors="replace"), "path")]
    seen, out = set(), []
    for lib in libs:
        k = str(lib).lower().rstrip("\\/")
        if k not in seen:
            seen.add(k)
            out.append(lib)
    return out


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def version_info(path: Path) -> dict:
    """Read VS_VERSIONINFO via version.dll (read-only Win32 API)."""
    try:
        ver = ctypes.windll.version
    except AttributeError:
        return {"error": "version.dll unavailable (non-Windows)"}
    size = ver.GetFileVersionInfoSizeW(str(path), None)
    if not size:
        return {"present": False}
    buf = ctypes.create_string_buffer(size)
    if not ver.GetFileVersionInfoW(str(path), 0, size, buf):
        return {"present": False}
    out = {"present": True}
    ptr, ln = ctypes.c_void_p(), ctypes.c_uint()
    if ver.VerQueryValueW(buf, "\\", ctypes.byref(ptr), ctypes.byref(ln)) and ln.value >= 52:
        fixed = ctypes.string_at(ptr, 52)
        ms, ls, pms, pls = (int.from_bytes(fixed[o:o + 4], "little") for o in (8, 12, 16, 20))
        out["file_version_fixed"] = f"{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}"
        out["product_version_fixed"] = f"{pms >> 16}.{pms & 0xFFFF}.{pls >> 16}.{pls & 0xFFFF}"
    if ver.VerQueryValueW(buf, "\\VarFileInfo\\Translation", ctypes.byref(ptr), ctypes.byref(ln)) and ln.value >= 4:
        t = ctypes.string_at(ptr, 4)
        lang = f"{int.from_bytes(t[0:2], 'little'):04x}{int.from_bytes(t[2:4], 'little'):04x}"
        for field in ("CompanyName", "ProductName", "FileDescription", "FileVersion", "ProductVersion",
                      "OriginalFilename", "InternalName", "LegalCopyright"):
            if ver.VerQueryValueW(buf, f"\\StringFileInfo\\{lang}\\{field}", ctypes.byref(ptr), ctypes.byref(ln)) and ln.value:
                out[field] = ctypes.wstring_at(ptr, ln.value).rstrip("\0")
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    a = ap.parse_args(argv)
    ev = a.repo / "evidence"
    now = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")

    steam = steam_root()
    if steam is None:
        print("blocker: Steam installation not found in registry", file=sys.stderr)
        return 3
    manifest = lib = None
    libs = libraries(steam)
    for l in libs:
        m = l / "steamapps" / f"appmanifest_{APP_ID}.acf"
        if m.is_file():
            manifest, lib = m, l
            break
    if manifest is None:
        print(f"blocker: appmanifest_{APP_ID}.acf not found in {len(libs)} libraries", file=sys.stderr)
        return 4

    mtext = manifest.read_text(encoding="utf-8", errors="replace")
    mf = {k: (vdf_values(mtext, k) or [None])[0] for k in
          ("installdir", "buildid", "TargetBuildID", "LastUpdated", "SizeOnDisk", "StateFlags")}
    game = lib / "steamapps" / "common" / mf["installdir"]
    x64 = game / "x64"
    exe = x64 / "ShadowOfWar.exe"

    def san(p: Path) -> str:
        s = str(p)
        for root, tag in ((game, "<GAME>"), (lib, "<LIBRARY>"), (steam, "<STEAM>")):
            if s.lower().startswith(str(root).lower()):
                return tag + s[len(str(root)):].replace("\\", "/")
        return "<OTHER>"

    files = sorted(p for p in x64.iterdir() if p.is_file())
    build = {
        "generated_utc": now,
        "tool": "tools/environment/discover_game.py",
        "steam_app_id": APP_ID,
        "steam_library_count": len(libs),
        "game_found_in_library_index": libs.index(lib),
        "manifest": {"path": san(manifest), **mf,
                     "LastUpdated_utc": dt.datetime.fromtimestamp(int(mf["LastUpdated"]), dt.timezone.utc).isoformat()
                     if mf["LastUpdated"] else None},
        "game_dir": "<GAME>",
        "executable": {"path": san(exe), "size": exe.stat().st_size, "sha256": sha256(exe),
                       "version_info": version_info(exe)},
        "x64_files": [{"name": p.name, "size": p.stat().st_size, "sha256": sha256(p),
                       "version": version_info(p).get("FileVersion")} for p in files],
        "top_level_archives": sorted(p.name for p in game.glob("*.arch06")),
        "proxy_dll_check": {n: (x64 / n).exists() for n in PROXY_DLLS},
        "uncertainties": [
            "buildid is Steam's depot build, not an engine version; exe hash is the authoritative identifier.",
            "Absence of proxy DLLs only covers the listed names; other injection methods are not checked.",
        ],
    }

    pe_inv = {"generated_utc": now, "tool": "tools/environment/pe_inventory.py", "files": {}}
    for p in files:
        if p.suffix.lower() in (".exe", ".dll"):
            try:
                rep = inventory(p)
            except (PEError, OSError) as e:
                rep = {"error": str(e)}
            if p.name != exe.name:  # keep DLL entries compact
                rep.pop("imports", None)
                rep["exports"] = {k: v for k, v in rep.get("exports", {}).items() if k != "names"}
            pe_inv["files"][p.name] = rep

    ev.mkdir(parents=True, exist_ok=True)
    (ev / "game-build.json").write_text(json.dumps(build, indent=2) + "\n", encoding="utf-8")
    (ev / "pe-inventory.json").write_text(json.dumps(pe_inv, indent=2) + "\n", encoding="utf-8")
    print(f"exe sha256 {build['executable']['sha256']} buildid {mf['buildid']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
