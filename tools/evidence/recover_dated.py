"""Review #17 (FB-17-02): recover test evidence that date-named files no longer hold.

Until 2026-10-25, scripts/test.ps1 copied suite results to evidence/logs/tests-<suite>-<date>.json and the E2E logs
to evidence/logs/e2e-<date>/, so a later run on the same day replaced what an earlier experiment record cited. Git
still holds every committed version. For each experiment record that cites such a path, this copies the version of
the cited file (or of every file under the cited directory) as it was in each commit that changed the record, into
evidence/logs/recovered/<EXP>/<commit>/, writes provenance.json, and points the record at the recovered directory.

Rule (mechanical, no judgement): the commit that created the record comes first (its results are what the record was
written from); later commits that edited the record are kept apart and labelled, because some edits describe extra
verification runs (EXP-023). A version identical to an earlier one is not copied again.

  python -I tools/evidence/recover_dated.py          # refuses to overwrite anything already recovered
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REC = ROOT / "evidence" / "logs" / "recovered"
# Mutable date-named evidence: one name per day, rewritten by every run that day.
DATED = re.compile(r"^evidence/logs/(tests-[a-z-]+-\d{4}-\d{2}-\d{2}\.json|e2e-\d{4}-\d{2}-\d{2}(/.*)?)/?$")  # e2e: the dir or a file in it


def git(*args: str, binary: bool = False):
    out = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, check=True).stdout
    return out if binary else out.decode("utf-8").strip()


def blobs_at(commit: str, path: str) -> dict[str, str]:
    """{repo path: blob id} for a file, or for every file under a directory, at a commit."""
    rows = git("ls-tree", "-r", commit, "--", path).splitlines()
    return {r.split("\t", 1)[1]: r.split()[2] for r in rows if "\t" in r}


def recover(rec_path: Path) -> dict | None:
    rec = json.loads(rec_path.read_text(encoding="utf-8"))
    dated = [p.rstrip("/") for p in rec.get("evidence_paths", []) if DATED.match(p)]
    if not dated:
        return None
    rel = rec_path.relative_to(ROOT).as_posix()
    commits = list(reversed(git("log", "--format=%H", "--", rel).split()))  # oldest (creation) first
    out_dir = REC / rec["id"]
    if out_dir.exists():
        raise SystemExit(f"{out_dir.relative_to(ROOT).as_posix()} already exists: recovered evidence is never overwritten")
    seen: dict[str, str] = {}  # (original path) -> last copied blob
    versions = []
    for i, c in enumerate(commits):
        files = []
        for p in dated:
            for src, blob in sorted(blobs_at(c, p).items()):
                if seen.get(src) == blob:
                    continue
                seen[src] = blob
                dst = out_dir / c[:7] / Path(src).relative_to("evidence/logs")
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_bytes(git("cat-file", "blob", blob, binary=True))
                files.append({"original_path": src, "blob": blob, "recovered_path": dst.relative_to(ROOT).as_posix()})
        missing = [p for p in dated if not blobs_at(c, p)]
        if files or (i == 0 and missing):
            versions.append({"commit": c, "role": "created" if i == 0 else "edited",
                             "subject": git("log", "-1", "--format=%s", c)[:160], "files": files,
                             "not_in_commit": missing if i == 0 else []})
    prov = {"experiment": rec["id"], "record": rel, "cited_dated_paths": dated,
            "rule": "version of each cited path in each commit that changed the record; creation first; identical versions not repeated",
            "versions": versions}
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "provenance.json").write_text(json.dumps(prov, indent=2) + "\n", encoding="utf-8", newline="\n")
    # Repoint: the dated paths are replaced by the recovered directory (kept in the record's original position).
    new_paths, inserted = [], False
    for p in rec["evidence_paths"]:
        if DATED.match(p):
            if not inserted:
                new_paths.append(out_dir.relative_to(ROOT).as_posix())
                inserted = True
        else:
            new_paths.append(p)
    rec["evidence_paths"] = new_paths
    eol = "\r\n" if b"\r\n" in rec_path.read_bytes() else "\n"  # keep the record's own line endings: no churn
    rec_path.write_text(json.dumps(rec, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline=eol)
    return prov


def main() -> int:
    if git("status", "--porcelain", "--", "evidence/experiments"):
        print("evidence/experiments has uncommitted changes: commit them first, so each record's history is complete")
        return 2
    total = 0
    for p in sorted((ROOT / "evidence" / "experiments").glob("EXP-*.json")):
        prov = recover(p)
        if prov:
            n = sum(len(v["files"]) for v in prov["versions"])
            total += n
            roles = ", ".join(f"{v['role']} {v['commit'][:7]} ({len(v['files'])})" for v in prov["versions"])
            print(f"{prov['experiment']}: {n} files: {roles}")
    print(f"recovered {total} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
