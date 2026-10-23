"""One-off: write F-ENV-* findings and EXP-001..003 from evidence/game-build.json + pe-inventory.json.

Re-runnable; regenerates the same records from the current inventory files.
"""
import datetime as dt
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EV = ROOT / "evidence"
build = json.loads((EV / "game-build.json").read_text(encoding="utf-8"))
exe = json.loads((EV / "pe-inventory.json").read_text(encoding="utf-8"))["files"]["ShadowOfWar.exe"]
SHA = build["executable"]["sha256"]
VER = build["executable"]["version_info"].get("FileVersion")
BID = build["manifest"]["buildid"]
TODAY = "2026-10-23"
GB = {"url": "", "commit": "", "path": "evidence/game-build.json", "location": ""}
PI = {"url": "", "commit": "", "path": "evidence/pe-inventory.json", "location": "files.ShadowOfWar.exe"}
COMPAT = f"Verified on build {BID}, exe {VER}, SHA-256 {SHA}"

sec = {s["name"]: s for s in exe["sections"]}
bind = sec.get(".bind", {})
exports = exe["exports"].get("names", [])
ak = sum("@AK@@" in n for n in exports)
sf = sum("Scaleform" in n for n in exports)
imp_dlls = [i["dll"].lower() for i in exe["imports"]]
ts = dt.datetime.fromtimestamp(exe["coff_tim