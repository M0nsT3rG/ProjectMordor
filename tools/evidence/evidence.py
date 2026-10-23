"""Evidence store tooling for Project Mordor.

  python -I tools/evidence/evidence.py validate   # schema + cross-reference checks, exit 1 on errors
  python -I tools/evidence/evidence.py index      # regenerate evidence/index.json and evidence/reports/index.md

Records live in evidence/findings/F-*.json and evidence/experiments/EXP-*.json.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EV = ROOT / "evidence"
CLASSES = {"CONFIRMED", "PARTIALLY_CONFIRMED", "HYPOTHESIS", "CONTRADICTED", "UNKNOWN"}
CONFIDENCE = {"high", "medium", "low"}
EXP_RESULTS = {"PASSED", "FAILED", "PARTIAL", "NOT_RUN", "ABANDONED"}
FINDING_KEYS = ["id", "type", "date", "subsystem", "claim", "evidence", "sources", "game_build_compatibility",
                "verification_method", "classification", "confidence", "limitations", "next_experiment", "related"]
EXP_KEYS = ["id", "type", "date", "objective", "hypothesis", "environment", "game_exe_sha256", "procedure",
            "expected_result", "observed_result", "evidence_paths", "result", "errors", "confidence",
            "follow_up", "related"]
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
# Absolute local paths must never be committed (rule 15).
ABS_PATH = re.compile(r"(?i)\b[a-z]:[\\/]|\\\\Users\\\\|/Users/|/home/")


def load_records() -> tuple[dict[str, dict], list[str]]:
    recs, errs = {}, []
    for sub, prefix in (("findings", "F-"), ("experiments", "EXP-")):
        for p in sorted((EV / sub).glob("*.json")):
            rel = p.relative_to(ROOT).as_posix()
            try:
                r = json.loads(p.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as e:
                errs.append(f"{rel}: unreadable JSON ({e})")
                continue
            if not isinstance(r, dict):
                errs.append(f"{rel}: top level must be an object")
                continue
            r["_path"] = rel
            rid = r.get("id", "")
            if not str(rid).startswith(prefix) or p.stem != rid:
                errs.append(f"{rel}: id '{rid}' must start with {prefix} and match the file name")
            if rid in recs:
                errs.append(f"{rel}: duplicate id {rid}")
            recs[rid] = r
    return recs, errs


def validate(recs: dict[str, dict]) -> list[str]:
    errs = []
    for rid, r in recs.items():
        where = r["_path"]
        is_f = r.get("type") == "finding"
        keys = FINDING_KEYS if is_f else EXP_KEYS
        if r.get("type") not in ("finding", "experiment"):
            errs.append(f"{where}: type must be 'finding' or 'experiment'")
            continue
        errs += [f"{where}: missing key '{k}'" for k in keys if k not in r]
        if not DATE.match(str(r.get("date", ""))):
            errs.append(f"{where}: date must be YYYY-MM-DD")
        if r.get("confidence") not in CONFIDENCE:
            errs.append(f"{where}: confidence must be one of {sorted(CONFIDENCE)}")
        for ref in r.get("related", []):
            if ref not in recs:
                errs.append(f"{where}: related id '{ref}' does not exist")
        text = json.dumps({k: v for k, v in r.items() if k != "_path"})
        if ABS_PATH.search(text):
            errs.append(f"{where}: contains an absolute local path")
        if is_f:
            if r.get("classification") not in CLASSES:
                errs.append(f"{where}: classification must be one of {sorted(CLASSES)}")
            srcs = r.get("sources")
            if not isinstance(srcs, list) or not srcs:
                errs.append(f"{where}: at least one source is required")
            for s in srcs or []:
                if not isinstance(s, dict) or not (s.get("url") or s.get("path")):
                    errs.append(f"{where}: each source needs a url or a path")
                    continue
                if "github.com" in s.get("url", "") and s.get("path") and not re.fullmatch(r"[0-9a-f]{40}", s.get("commit", "")):
                    errs.append(f"{where}: repository file sources need a full 40-char commit hash")
                local = s.get("url", "") == "" and s.get("path", "")
                if local and not (ROOT / local).exists():
                    errs.append(f"{where}: local source '{local}' not found")
        else:
            if r.get("result") not in EXP_RESULTS:
                errs.append(f"{where}: result must be one of {sorted(EXP_RESULTS)}")
            for ep in r.get("evidence_paths", []):
                if not (ROOT / ep).exists():
                    errs.append(f"{where}: evidence path '{ep}' not found")
    return errs


def build_index(recs: dict[str, dict]) -> None:
    rows = []
    for rid in sorted(recs):
        r = recs[rid]
        rows.append({"id": rid, "type": r["type"], "path": r["_path"], "date": r.get("date"),
                     "subsystem": r.get("subsystem", "experiment"),
                     "status": r.get("classification") or r.get("result"),
                     "confidence": r.get("confidence"),
                     "summary": r.get("claim") or r.get("objective")})
    (EV / "index.json").write_text(json.dumps({"generated_by": "tools/evidence/evidence.py index",
                                               "count": len(rows), "records": rows}, indent=2) + "\n", encoding="utf-8")
    md = ["# Evidence index", "", "Generated by `python -I tools/evidence/evidence.py index`. Do not edit by hand.", ""]
    for kind, title in (("finding", "Findings"), ("experiment", "Experiments")):
        sel = [x for x in rows if x["type"] == kind]
        md += [f"## {title} ({len(sel)})", "", "| ID | Subsystem | Status | Confidence | Summary |", "|---|---|---|---|---|"]
        md += [f"| [{x['id']}](../../{x['path']}) | {x['subsystem']} | {x['status']} | {x['confidence']} | "
               f"{str(x['summary']).replace('|', '/')} |" for x in sel]
        md.append("")
    counts: dict[str, int] = {}
    for x in rows:
        counts[x["status"]] = counts.get(x["status"], 0) + 1
    md += ["## Status totals", ""] + [f"- {k}: {v}" for k, v in sorted(counts.items())] + [""]
    (EV / "reports").mkdir(exist_ok=True)
    (EV / "reports" / "index.md").write_text("\n".join(md), encoding="utf-8")


def main(argv: list[str]) -> int:
    if not argv or argv[0] not in ("validate", "index"):
        print(__doc__)
        return 2
    recs, errs = load_records()
    errs += validate(recs)
    if argv[0] == "index":
        build_index(recs)
        print(f"index written: {len(recs)} records")
    for e in errs:
        print(f"ERROR {e}")
    print(f"{len(recs)} records, {len(errs)} errors")
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
