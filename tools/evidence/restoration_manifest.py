"""FB-22-02 / FB-23-01: provenance of per-invocation test evidence, checked file by file against run.json.

Until 2fb595a, .gitignore's unanchored `build-*/` also matched evidence/logs/tests-*/build-gns-iter-1/, so those
directories were written by scripts/test.ps1 during each run but committed later than the rest of the run. For every
per-run test directory, each ctest invocation that run.json records is compared FILE BY FILE with what the invocation
says it wrote (`files`, plus `<suite>.invalid.json` for every `invalid` entry):
  - each expected file is `present` or `missing`; a file on disk that run.json does not list is `unexpected`;
  - each present file has its own first commit, and `restored: true` when that is later than the run's run.json;
  - the invocation is `complete`, `partial` or `missing`.
SHA-256 digests are computed now: they prove consistency from restoration onward, NOT contemporaneous authenticity.

  python -I tools/evidence/restoration_manifest.py   # writes evidence/logs/restored/manifest-v2.json (never overwrites)
manifest.json (v1, review #22) stays as written: it classified a directory by its first file only (FB-23-01).
"""
from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOGS = ROOT / "evidence" / "logs"
OUT = LOGS / "restored" / "manifest-v2.json"
RUN_DIR = re.compile(r"^tests-\d{4}-\d{2}-\d{2}-\d{6}-[0-9a-f]{7}(-dirty)?$")


def first_commit(path: str) -> str | None:
    out = subprocess.run(["git", "log", "--diff-filter=A", "--format=%h", "--", path], cwd=ROOT,
                         capture_output=True, text=True, check=True).stdout.split()
    return out[-1] if out else None


def is_later(a: str, b: str) -> bool:
    """True when commit a is a strict descendant of commit b."""
    return a != b and subprocess.run(["git", "merge-base", "--is-ancestor", b, a], cwd=ROOT).returncode == 0


def invocation(run_dir: Path, run_commit: str | None, c: dict) -> dict:
    sub = str(c.get("dir"))
    expected = [str(f) for f in (c.get("files") or [])] + [f"{sub}/{k}.invalid.json" for k in (c.get("invalid") or {})]
    on_disk = {p.relative_to(run_dir).as_posix() for p in (run_dir / sub).glob("*") if p.is_file()}
    files = []
    for rel in sorted(set(expected) | on_disk):
        p = run_dir / rel
        entry = {"path": (p.relative_to(ROOT)).as_posix(), "status": "present" if rel in on_disk else "missing"}
        if rel not in expected:
            entry["status"] = "unexpected"
        if p.is_file():
            data = p.read_bytes()
            fc = first_commit(entry["path"])
            entry.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), first_committed=fc,
                         restored=bool(fc and run_commit and is_later(fc, run_commit)))
        files.append(entry)
    present = sum(f["status"] == "present" for f in files)
    status = "complete" if present == len(expected) and not any(f["status"] == "unexpected" for f in files) else \
             "missing" if present == 0 else "partial"
    return {"dir": sub, "status": status, "expected": len(expected), "present": present, "files": files}


def build() -> dict:
    runs, dirs_by_commit = [], Counter()
    for d in sorted(p for p in LOGS.glob("tests-*") if p.is_dir() and RUN_DIR.match(p.name)):
        try:
            run = json.loads((d / "run.json").read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        rel = d.relative_to(ROOT).as_posix()
        run_commit = first_commit(f"{rel}/run.json")
        invs = [invocation(d, run_commit, c) for c in run.get("ctest_runs") or []]
        for i in invs:
            commits = {f.get("first_committed") for f in i["files"] if f.get("first_committed")}
            if i["dir"].startswith("build-gns-iter-") and commits:
                dirs_by_commit[min(commits)] += 1
        if any(i["status"] != "complete" or any(f.get("restored") for f in i["files"]) for i in invs):
            runs.append({"run": rel, "run_first_committed": run_commit, "invocations": invs})
    return {
        "about": "Per-file provenance of per-invocation test evidence, checked against run.json (FB-23-01).",
        "digest_note": "SHA-256 computed at restoration: consistency from restoration onward, not contemporaneous authenticity.",
        "scope_note": "Only invocations recorded in a run.json ctest_runs list are covered; runs before per-invocation records have none.",
        "build_gns_dirs_by_first_commit": dict(dirs_by_commit),
        "runs": runs,
    }


def main() -> int:
    if OUT.exists():
        print(f"{OUT.relative_to(ROOT).as_posix()} exists: evidence manifests are never overwritten")
        return 1
    m = build()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(m, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("build-gns-iter-* directories by first commit:", m["build_gns_dirs_by_first_commit"])
    for r in m["runs"]:
        for i in r["invocations"]:
            restored = sum(bool(f.get("restored")) for f in i["files"])
            print(f"{r['run'].split('/')[-1]} {i['dir']}: {i['status']} {i['present']}/{i['expected']}, restored files {restored}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
