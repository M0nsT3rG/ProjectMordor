"""Local progress reporter for the ChatGPT collaboration bridge.

Reads git state, evidence/ records and real test logs, then regenerates:
  .ai-collaboration/PROJECT_STATUS.md   (only the AUTO block)
  .ai-collaboration/TEST_RESULTS.md
  .ai-collaboration/RESEARCH_FINDINGS.md
and prints a concise review summary.

Runs only when invoked. No network access, no daemon. It never invents results:
anything without a log file is reported as missing.

Usage: python -I tools/collab/report.py [--repo PATH] [--dry-run]
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

AUTO_BEGIN = "<!-- AUTO:BEGIN"
AUTO_END = "<!-- AUTO:END -->"
RUN_DIR = re.compile(r"^tests-(\d{4}-\d{2}-\d{2}-\d{6})-[0-9a-f]{7}(-dirty)?$")  # test.ps1 per-run directory (FB-17-02)
RECENT_RUNS = 5  # FB-19-01: a short history, not every run ever
SUITES = ("unit", "integration", "network")  # required (UDP / default build)
# Reported when present: contract suite and per-transport GNS results (review #4 next steps).
OPTIONAL_SUITES = ("transport", "transport-both", "integration-gns", "network-gns", "gns", "dtls")
# Keep simulated-clock and real-time results visibly separate (review FB-1-03).
CLOCK = {"unit": "none (pure logic)", "integration": "simulated clock, real loopback UDP",
         "network": "simulated clock, real loopback UDP, seeded impairment",
         "transport": "real time, loopback, UDP backend", "transport-both": "real time, loopback, UDP + GNS backends",
         "integration-gns": "simulated clock coupled to real time, GNS backend",
         "network-gns": "simulated clock coupled to real time, GNS backend, GNS fake impairment",
         "gns": "real time, raw GNS spike",
         "dtls": "in-memory datagrams, OpenSSL DTLS 1.2 + RFC 7250 proof (D-013), no network"}


def git(repo: Path, *args: str) -> str:
    try:
        r = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as e:
        return f"<git unavailable: {e}>"
    return r.stdout.strip() if r.returncode == 0 else ""


def git_state(repo: Path) -> dict:
    return {
        "branch": git(repo, "rev-parse", "--abbrev-ref", "HEAD") or "<no commits>",
        "head": git(repo, "log", "-1", "--format=%h %cs %s") or "<no commits>",
        "head_date": git(repo, "log", "-1", "--format=%cs"),
        "remotes": sorted({l.split()[1] for l in git(repo, "remote", "-v").splitlines() if l.strip()}),
        "changes": [l for l in git(repo, "status", "--porcelain").splitlines() if l.strip()],
    }


def load_json_dir(d: Path) -> list[dict]:
    out = []
    for p in sorted(d.glob("*.json")):
        try:
            out.append(json.loads(p.read_text(encoding="utf-8")))
        except (OSError, json.JSONDecodeError):
            out.append({"id": p.stem, "_unreadable": True})
    return out


def latest_test_logs(logs: Path, suites=SUITES) -> dict[str, dict]:
    """Per suite: the newest result, parsed into passed / failed / skipped. SKIPPED is never PASSED.
    Newest of: <suite>.json in a per-run directory tests-YYYY-MM-DD-HHMMSS-<commit>[-dirty]/ (FB-17-02), or a legacy
    tests-<suite>-YYYY-MM-DD.json (exact name, so 'integration' never matches 'integration-gns' or '*-first-run').
    Ordered by the timestamp in the name; a legacy day sorts before any run directory of that day."""
    found = {}
    runs = [(m.group(1), d) for d in logs.glob("tests-*") if d.is_dir() and (m := RUN_DIR.match(d.name))]
    for suite in suites:
        pat = re.compile(rf"^tests-{re.escape(suite)}-(\d{{4}}-\d{{2}}-\d{{2}})\.json$")
        cands = [(m.group(1), p) for p in logs.glob("tests-*.json") if (m := pat.match(p.name))]
        cands += [(ts, d / f"{suite}.json") for ts, d in runs if (d / f"{suite}.json").is_file()]
        if not cands:
            continue
        ts, p = max(cands, key=lambda c: c[0])
        rel = p.relative_to(logs).as_posix()  # a run directory's file is <run>/<suite>.json
        try:
            cases = json.loads(p.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            found[suite] = {"file": rel, "error": "unreadable"}
            continue
        failed = [c["name"] for c in cases if c.get("result") not in ("PASSED", "SKIPPED")]
        skipped = [c["name"] for c in cases if c.get("result") == "SKIPPED"]
        found[suite] = {"file": rel, "date": ts, "total": len(cases), "run": run_info(logs, rel),
                        "passed": len(cases) - len(failed) - len(skipped), "failed": failed, "skipped": skipped}
    return found


def run_info(logs: Path, rel: str) -> dict | None:
    """FB-18-02: the run a suite file came from (result, commit, dirty), so a passing suite from a failed or
    incomplete run is never shown as a clean pass. None for a legacy date-named file, which has no run record."""
    if "/" not in rel:
        return None
    try:
        r = json.loads((logs / rel.split("/")[0] / "run.json").read_text(encoding="utf-8"))
        return {"result": str(r.get("result", "unknown")), "commit": str(r.get("commit", ""))[:7], "dirty": bool(r.get("dirty"))}
    except (OSError, ValueError, AttributeError):  # ValueError covers JSONDecodeError and invalid UTF-8
        return {"result": "unknown (no readable run.json)", "commit": "", "dirty": False}


def recent_runs(logs: Path, n: int = RECENT_RUNS) -> list[str]:
    """FB-19-01: the newest test.ps1 runs, each with its overall result and EVERY ctest invocation's exit code and
    missing suites, so a later pass never hides an earlier failed invocation in the same run."""
    lines = []
    for d in sorted((d for d in logs.glob("tests-*") if d.is_dir() and RUN_DIR.match(d.name)), key=lambda d: d.name)[-n:][::-1]:
        try:
            r = json.loads((d / "run.json").read_text(encoding="utf-8"))
            calls = ", ".join(f"{c.get('dir')}: exit {c.get('exit_code')}" + (f", MISSING {'/'.join(c['missing'])}" if c.get("missing") else "")
                              for c in r.get("ctest_runs") or [])
            res = str(r.get("result", "unknown"))
            lines.append(f"- `{d.name}`: {res if res == 'passed' else f'**{res}**'} ({calls or ('no ctest invocation recorded' if 'ctest_runs' in r else 'before per-invocation records')})")
        except (OSError, ValueError, AttributeError, TypeError):  # ValueError covers invalid UTF-8 too
            lines.append(f"- `{d.name}`: **no readable run.json**")
    return lines


def run_note(t: dict) -> str:
    r = t.get("run")
    if not r:
        return " (legacy date-named file, no run record)"
    s = f"run {r['result']}" + (f", {r['commit']}" if r["commit"] else "") + (", dirty" if r["dirty"] else "")
    return f" [{s}]" if r["result"] == "passed" else f" [**{s}**]"


def replace_auto_block(text: str, body: str) -> str:
    start, end = text.find(AUTO_BEGIN), text.find(AUTO_END)
    block = f"{AUTO_BEGIN} (generated by tools/collab/report.py; do not edit by hand) -->\n{body.rstrip()}\n{AUTO_END}"
    if start == -1 or end == -1 or end < start:
        return text.rstrip() + "\n\n" + block + "\n"
    return text[:start] + block + text[end + len(AUTO_END):]


def md_cell(v) -> str:
    return str(v).replace("|", "/").replace("\n", " ").strip()


def build(repo: Path) -> dict[str, str]:
    g = git_state(repo)
    ev = repo / "evidence"
    findings = [f for f in load_json_dir(ev / "findings") if f.get("type") == "finding"]
    exps = [e for e in load_json_dir(ev / "experiments") if e.get("type") == "experiment"]
    tests = latest_test_logs(ev / "logs")
    # Legacy e2e-<date>/ plus, since FB-17-02, the e2e/ inside each per-run test directory (Codex review).
    e2e = sorted(p.name for p in (ev / "logs").glob("e2e-*") if p.is_dir()) +         sorted(f"{d.name}/e2e" for d in (ev / "logs").glob("tests-*") if (d / "e2e").is_dir())

    cls = Counter(f.get("classification", "?") for f in findings)
    res = Counter(e.get("result", "?") for e in exps)
    total = sum(t.get("total", 0) for t in tests.values())
    passed = sum(t.get("passed", 0) for t in tests.values())
    missing = [s for s in SUITES if s not in tests]

    test_lines = [f"- {s}: {t['passed']}/{t['total']} passed ({t['file']}){run_note(t)}" + (f"; FAILED: {', '.join(t['failed'])}" if t.get("failed") else "")
                  if "total" in t else f"- {s}: log unreadable ({t['file']})" for s, t in tests.items()]
    test_lines += [f"- {s}: **no test log found**" for s in missing]

    status = "\n".join([
        "### Generated snapshot",
        "",
        f"- Branch: `{g['branch']}`; HEAD: `{g['head']}`",
        f"- Remotes: {', '.join(g['remotes']) if g['remotes'] else 'none configured'}",
        f"- Uncommitted changes: {len(g['changes'])} path(s)",
        f"- Evidence: {len(findings)} findings ({', '.join(f'{k} {v}' for k, v in sorted(cls.items()))}); "
        f"{len(exps)} experiments ({', '.join(f'{k} {v}' for k, v in sorted(res.items()))})",
        f"- Latest automated tests: {passed}/{total} passed" + (f"; missing suites: {', '.join(missing)}" if missing else ""),
        *test_lines,
        f"- Recent test runs (newest first, at most {RECENT_RUNS}):",
        *(["  " + x for x in recent_runs(ev / "logs")] or ["  - none"]),
        f"- End-to-end runs logged: {', '.join(e2e) if e2e else 'none'}",
    ])

    tr = ["# Test results", "", "Generated by `python -I tools/collab/report.py` from `evidence/logs/` and `evidence/experiments/`. "
          "Only logged results appear here; a suite without a log is reported as missing.", "",
          f"Repository HEAD when generated: `{g['head']}` ({len(g['changes'])} uncommitted path(s)).", "",
          "## Automated suites (latest log per suite)", "", "| Suite | Clock | Passed | Skipped | Total | Failed tests | Log |",
          "|---|---|---|---|---|---|---|"]
    optional = latest_test_logs(ev / "logs", OPTIONAL_SUITES)
    for s in (*SUITES, *optional):
        t = tests.get(s) or optional.get(s)
        if not t:
            tr.append(f"| {s} | {CLOCK[s]} | - | - | - | no log found | - |")
        elif "total" not in t:
            tr.append(f"| {s} | {CLOCK[s]} | - | - | - | log unreadable | `{t['file']}` |")
        else:
            tr.append(f"| {s} | {CLOCK[s]} | {t['passed']} | {len(t['skipped'])} | {t['total']} | "
                      f"{md_cell(', '.join(t['failed']) or 'none')} | `evidence/logs/{t['file']}`{md_cell(run_note(t))} |")
    tr += ["", "SKIPPED tests are listed with their reason in the JSON log; a skip is never counted as a pass."]
    tr += ["", f"Recent test runs, newest first (at most {RECENT_RUNS}), with every ctest invocation:", ""] + (recent_runs(ev / "logs") or ["- none"])
    tr += ["", "**Real-time** results (wall clock, separate processes) come only from end-to-end and soak runs, listed below; "
           "LAN results only from two-PC experiments. Simulated-clock passes do not imply real-time behaviour."]
    tr += ["", "## Real-time soak runs (wall clock, separate processes)", "",
           "| Run | Gates | client1 RTT p50/p99 ms | Recovery after stall ms | Shed inputs | Summary |", "|---|---|---|---|---|---|"]
    soaks = sorted(p for p in (ev / "logs").glob("soak-*") if p.is_dir())
    for d in soaks:
        try:
            s = json.loads((d / "summary.json").read_text(encoding="utf-8"))
            c1, c2 = s["clients"].get("client1", {}), s["clients"].get("client2", {})
            failed = [g["gate"] for g in s["gates"] if not g["passed"]]
            verdict = "passed" if s["passed"] else "FAILED: " + ", ".join(failed)
            tr.append(f"| {d.name} | v{s.get('gate_version', 1)} {verdict} ({len(s['gates'])} gates) | "
                      f"{c1.get('rtt_p50_ms')} / {c1.get('rtt_p99_ms')} | {c2.get('recovery_after_stall_ms')} | "
                      f"{s['host'].get('shed_inputs')} | `evidence/logs/{d.name}/summary.json` |")
        except (OSError, KeyError, json.JSONDecodeError):
            tr.append(f"| {d.name} | **no readable summary.json** | - | - | - | - |")
    if not soaks:
        tr.append("| none | - | - | - | - | - |")
    tr += ["", "## Experiments", "", "| ID | Result | Objective |", "|---|---|---|"]
    tr += [f"| {e.get('id')} | {e.get('result')} | {md_cell(e.get('objective', ''))} |" for e in exps]
    tr += ["", f"End-to-end log directories: {', '.join(f'`evidence/logs/{d}/`' for d in e2e) if e2e else 'none'}", ""]

    rf = ["# Research findings", "",
          "Generated by `python -I tools/collab/report.py` from `evidence/findings/*.json`, which is the source of truth. "
          "**Only CONFIRMED rows are verified, and only within their `scope`:** `locally_measured` = measured by us on this machine; "
          "`source_verified` = we checked what a repository/page itself says or contains (not proof about the game); "
          "`third_party_claim` = someone else's claim about the game, never CONFIRMED. "
          "PARTIALLY_CONFIRMED and HYPOTHESIS rows are not verified discoveries.", "",
          "Reproduce: each record's `verification_method` field describes how it was obtained; `python -I tools/evidence/evidence.py validate` checks the store.", "",
          f"Totals: {', '.join(f'{k} {v}' for k, v in sorted(cls.items()))}.", ""]
    order = ["CONFIRMED", "PARTIALLY_CONFIRMED", "HYPOTHESIS", "CONTRADICTED", "UNKNOWN"]
    for c in order + sorted(set(cls) - set(order)):
        sel = [f for f in findings if f.get("classification") == c]
        if not sel:
            continue
        rf += [f"## {c} ({len(sel)})", "", "| ID | Scope | Subsystem | Confidence | Claim | Source | Build compatibility | Implication / next step |",
               "|---|---|---|---|---|---|---|---|"]
        for f in sel:
            src = (f.get("sources") or [{}])[0]
            loc = src.get("url") or src.get("path") or ""
            if src.get("commit"):
                loc += f" @ {src['commit'][:10]}"
            if src.get("path") and src.get("url"):
                loc += f" `{src['path']}`"
            rf.append(f"| {f.get('id')} | {f.get('verification_scope', 'MISSING')} | {f.get('subsystem')} | {f.get('confidence')} | {md_cell(f.get('claim'))} | {md_cell(loc)} | "
                      f"{md_cell(f.get('game_build_compatibility', ''))[:160]} | {md_cell(f.get('next_experiment', ''))[:200]} |")
        rf.append("")

    summary = "\n".join([
        f"Project Mordor review summary: {g['branch']} @ {g['head']}",
        f"  uncommitted paths: {len(g['changes'])}",
        f"  tests: {passed}/{total} passed" + (f" (missing: {', '.join(missing)})" if missing else ""),
        f"  failed tests: {sum(len(t.get('failed', [])) for t in tests.values())}",
        f"  findings: {len(findings)}  experiments: {len(exps)} ({', '.join(f'{k} {v}' for k, v in sorted(res.items()))})",
        f"  remotes: {', '.join(g['remotes']) if g['remotes'] else 'none'}",
    ])
    return {"status_block": status, "test_results": "\n".join(tr), "findings": "\n".join(rf), "summary": summary}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    ap.add_argument("--dry-run", action="store_true", help="print the summary without writing files")
    a = ap.parse_args(argv)
    collab = a.repo / ".ai-collaboration"
    out = build(a.repo)
    if not a.dry_run:
        status_path = collab / "PROJECT_STATUS.md"
        if not status_path.is_file():
            print(f"error: {status_path.relative_to(a.repo)} not found", file=sys.stderr)
            return 2
        status_path.write_text(replace_auto_block(status_path.read_text(encoding="utf-8"), out["status_block"]), encoding="utf-8")
        (collab / "TEST_RESULTS.md").write_text(out["test_results"], encoding="utf-8")
        (collab / "RESEARCH_FINDINGS.md").write_text(out["findings"], encoding="utf-8")
    print(out["summary"])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
