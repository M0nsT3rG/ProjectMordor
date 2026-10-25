"""FB-22-02: provenance of per-invocation test evidence that was restored, and of any that is missing.

Until 2fb595a, .gitignore's unanchored `build-*/` also matched evidence/logs/tests-*/build-gns-iter-1/, so those
directories were written by scripts/test.ps1 during each run but never committed with the rest of the run. They stayed
on disk, unmodified, and were committed later. For every per-run test directory this lists each ctest invocation that
run.json records: when its files were first committed, whether that was after the rest of the run (restored), their
SHA-256 digests, and any invocation whose directory is absent (missing, cannot be reconstructed).

  python -I tools/evidence/restoration_manifest.py   # writes evidence/logs/restored/manifest.json (refuses to overwrite)
Digests are computed at restoration time: no earlier digest exists, so they prove integrity from then on, not from the run.
"""
from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOGS = ROOT / "evidence" / "logs"
OUT = LOGS / "restored" / "manifest.json"
RUN_DIR = re.compile(r"^tests-\d{4}-\d{2}-\d{2}-\d{6}-[0-9a-f]{7}(-dirty)?$")


def first_commit(path: str) -> str | None:
    out = subprocess.run(["git", "log", "--diff-filter=A", "--format=%h", "--", path], cwd=ROOT,
                         capture_output=True, text=True, check=True).stdout.split()
    return out[-1] if out else None


def build() -> dict:
    runs = []
    for d in sorted(p for p in LOGS.glob("tests-*") if p.is_dir() and RUN_DIR.match(p.name)):
        rel = d.relative_to(ROOT).as_posix()
        try:
            run = json.loads((d / "run.json").read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        run_commit = first_commit(f"{rel}/run.json")
        invocations = []
        for c in run.get("ctest_runs") or []:
            sub = d / str(c.get("dir"))
            files = sorted(p for p in sub.glob("*") if p.is_file()) if sub.is_dir() else []
            entry = {"dir": c.get("dir"), "status": "present", "first_committed": None, "files": []}
            if not files:
                entry["status"] = "missing"  # recorded in run.json, absent on disk: cannot be reconstructed
            for p in files:
                prel = p.relative_to(ROOT).as_posix()
                entry["files"].append({"path": prel, "bytes": p.stat().st_size,
                                       "sha256": hashlib.sha256(p.read_bytes()).hexdigest()})
                entry["first_committed"] = entry["first_committed"] or first_commit(prel)
            if files and entry["first_committed"] != run_commit:
                entry["status"] = "restored"  # committed after the rest of its run, from the unmodified files on disk
            invocations.append(entry)
        if any(i["status"] != "present" for i in invocations):
            runs.append({"run": rel, "run_first_committed": run_commit, "invocations": invocations})
    return {"about": __doc__.split("\n\n")[1].replace("\n", " "),
            "digest_note": "SHA-256 computed at restoration; there is no earlier digest to compare with.",
            "runs": runs}


def main() -> int:
    if OUT.exists():
        print(f"{OUT.relative_to(ROOT).as_posix()} exists: evidence manifests are never overwritten")
        return 1
    m = build()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(m, indent=2) + "\n", encoding="utf-8", newline="\n")
    for r in m["runs"]:
        print(r["run"], [(i["dir"], i["status"], i["first_committed"], len(i["files"])) for i in r["invocations"] if i["status"] != "present"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
