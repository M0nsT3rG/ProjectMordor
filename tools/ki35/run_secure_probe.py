"""EXP-037 runner (review #25): runs ki35_secure_probe in the predeclared batches, records everything immutably, and
applies the rules predeclared in docs/soak-testing.md ("Predeclared experiment KI-35: DTLS and session phases").
  python -I tools/ki35/run_secure_probe.py build-gns/Release/ki35_secure_probe.exe [trials_per_batch]
Writes evidence/logs/ki35-secure-<time>-<commit7>[-dirty]/ (per-batch trials and client logs, verdict.json); never
overwrites. --analyze DIR re-reads a recorded directory without running anything.
"""
from __future__ import annotations

import datetime
import json
import math
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bounded import run_bounded  # noqa: E402
# Fixed in the predeclaration (tools/ki35/model.py proportions()); never tuned to results.
MODEL = {"dtls_no_timer": [0.3771, 0.3944], "session_first": [0.6141, 0.628], "session_first_2leg": [0.7225, 0.7334],
         "gns_first_cycle": [0.522, 0.5378]}
BATCHES = [("udp", 3701), ("udp", 3702), ("udp", 3703), ("gns", 1), ("gns", 2), ("gns", 3)]
GNS_FIRST_CYCLE_MS = 800.0  # EXP-036 classification: inner Connected within the first 500 ms retry cycle


def wilson(k: int, n: int, z: float = 1.96) -> list[float] | None:
    if n == 0:
        return None
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return [round(c - h, 4), round(c + h, 4)]


def overlaps(a: list[float], b: list[float]) -> bool:
    return a[0] <= b[1] and b[0] <= a[1]


def pct(xs: list[float], p: int) -> float | None:
    s = sorted(xs)
    return round(s[min(len(s) - 1, int(p / 100 * len(s)))], 1) if s else None


def load_trials(d: Path) -> list[dict]:
    lines = (d / "trials.jsonl").read_text(encoding="utf-8").splitlines()
    return [json.loads(l) for l in lines if l.strip().endswith("}")]  # a hung probe's last line may be cut


def load_batch(d: Path) -> list[dict]:
    trials = load_trials(d)
    log = d / "client.log"
    if not log.exists():  # unavailable, not zero requests: session_first then has n = 0 and no interval
        return trials
    reqs = [json.loads(l)["t_ms"] for l in log.read_text(encoding="utf-8").splitlines()
            if l.strip() and '"evt":"connect_request"' in l]
    for t in trials:  # ConnectRequests after authentication, up to Connected or the probe's 20 s deadline (1 ms log resolution)
        if t["auth"] >= 0:
            lo, hi = t["log_ms"] + t["auth"] - 2, t["log_ms"] + (t["connected"] if t["ok"] else 20_000) + 1
            t["session_requests"] = sum(lo <= x <= hi for x in reqs)
    return trials


def summarize(trials: list[dict], backend: str) -> dict:
    ok = [t for t in trials if t["ok"]]
    phases = {"establish": [t["first_hello"] for t in ok], "dtls": [t["auth"] - t["first_hello"] for t in ok],
              "session": [t["connected"] - t["auth"] for t in ok], "total": [t["connected"] for t in ok]}
    lat = {k: {"n": len(v), **{f"p{p}": pct(v, p) for p in (50, 90, 95, 99)}} for k, v in phases.items()}
    # Each proportion counts every trial that reached its phase, whatever happened after (no conditioning on success).
    dtls = [t for t in trials if t["first_hello"] >= 0]
    session = [t for t in trials if "session_requests" in t]
    props = {"dtls_no_timer": (sum(t["auth"] >= 0 and t["timers"] == 0 for t in dtls), len(dtls)),
             "session_first": (sum(t["ok"] and t["session_requests"] == 1 for t in session), len(session))}
    if backend == "gns":
        props["gns_first_cycle"] = (sum(0 <= t["inner"] < GNS_FIRST_CYCLE_MS for t in trials), len(trials))
    slow = sorted(ok, key=lambda t: t["connected"])[-max(1, len(ok) // 10):]
    largest = {}
    for t in slow:
        parts = {"establish": t["first_hello"], "dtls": t["auth"] - t["first_hello"], "session": t["connected"] - t["auth"]}
        k = max(parts, key=parts.get)
        largest[k] = largest.get(k, 0) + 1
    loops = [t["loop_ms"] for t in ok if t.get("loop_ms", -1) > 0]  # absent before the timer fix (Codex, PR #25)
    return {"n": len(trials), "failures": len(trials) - len(ok), "latency_ms": lat,
            "loop_ms": {"n": len(loops), "p50": pct(loops, 50), "p99": pct(loops, 99)},
            "proportions": {k: {"k": a, "n": b, "p": round(a / b, 4) if b else None, "wilson95": wilson(a, b)} for k, (a, b) in props.items()},
            "slowest_10pct_largest_phase": largest}


def judge(out: Path) -> dict:
    per_batch, pooled = {}, {"udp": [], "gns": []}
    for d in sorted(p for p in out.iterdir() if p.is_dir()):
        backend = d.name.split("-")[0]
        trials = load_batch(d)
        per_batch[d.name] = summarize(trials, backend)
        pooled[backend] += trials
    verdict = {"model": MODEL, "batches": per_batch, "pooled": {}, "rules": {}}
    for backend, trials in pooled.items():
        if not trials:
            continue
        s = summarize(trials, backend)
        verdict["pooled"][backend] = s
        rules = {}
        for name, pr in s["proportions"].items():
            ci = pr["wilson95"]
            rules[name] = {"wilson95": ci, "model": MODEL[name], "available": ci is not None,
                           "supported": bool(ci and overlaps(ci, MODEL[name]))}
            if name == "session_first":
                rules[name]["intersects_3leg"] = bool(ci and overlaps(ci, MODEL["session_first"]))
                rules[name]["intersects_2leg"] = bool(ci and overlaps(ci, MODEL["session_first_2leg"]))
        cis = {name: [b["proportions"][name]["wilson95"] for k, b in per_batch.items() if k.startswith(backend)
                      and b["proportions"].get(name, {}).get("wilson95")] for name in s["proportions"]}
        rules["batches_consistent"] = {name: all(overlaps(a, b) for i, a in enumerate(v) for b in v[i + 1:]) for name, v in cis.items()}
        verdict["rules"][backend] = rules
    return verdict


def main(argv: list[str]) -> int:
    if argv[:1] == ["--analyze"] and len(argv) == 2:
        print(json.dumps(judge(Path(argv[1])), indent=2))
        return 0
    if not argv or not Path(argv[0]).is_file():
        print(__doc__)
        return 2
    trials = argv[1] if len(argv) > 1 else "100"
    git = lambda *a: subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    commit = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain", "--", ".", ":(exclude)evidence"))
    started = datetime.datetime.now()
    out = ROOT / "evidence" / "logs" / f"ki35-secure-{started:%Y-%m-%d-%H%M%S}-{commit[:7]}{'-dirty' if dirty else ''}"
    out.mkdir(parents=True)  # raises if it exists: evidence is never overwritten
    exe = str(Path(argv[0]).resolve())
    codes = {}
    for backend, seed in BATCHES:
        d = out / f"{backend}-{seed}"
        d.mkdir()
        codes[d.name] = run_bounded([exe, backend, trials, str(seed), str(d / "client.log")], d / "trials.jsonl", 7200)
        print(f"{d.name}: exit {codes[d.name]}, {len(load_trials(d))} trials", flush=True)
        # rewritten after every batch, so an interrupted battery keeps its provenance
        (out / "run.json").write_text(json.dumps({"started": started.isoformat(timespec="seconds"), "commit": commit, "dirty": dirty,
                                                  "trials_per_batch": int(trials), "batches": BATCHES, "exit_codes": codes}, indent=2) + "\n",
                                      encoding="utf-8")
    verdict = judge(out)
    (out / "verdict.json").write_text(json.dumps(verdict, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(verdict["rules"], indent=2))
    print(f"recorded: {out.relative_to(ROOT).as_posix()}")
    return 0 if all(c == 0 for c in codes.values()) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
