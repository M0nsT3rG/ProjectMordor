"""EXP-043 runner: A/B of client-side gating (confirm_binding off / on). Runs ki35_reconnect_probe in the predeclared
batches (arms alternating), records everything immutably, and applies the rules predeclared in docs/soak-testing.md
("Predeclared experiment KI-35: A/B of client-side gating").
  python -I tools/ki35/run_ab_probe.py build-gns/Release/ki35_reconnect_probe.exe
Writes evidence/logs/ki35-ab-<time>-<commit7>[-dirty]/ (per-batch cycles and client logs, run.json, verdict.json);
never overwrites. --analyze DIR re-reads a recorded directory without running anything.
"""
from __future__ import annotations

import datetime
import json
import math
import random
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_reconnect_probe as rc  # noqa: E402  (loading, attribution, Wilson, homogeneity, percentiles)
import run_reorder_probe as rr  # noqa: E402  (D1 classification with the host trace)
from bounded import run_bounded  # noqa: E402

ROOT = rc.ROOT
LIMIT_MS = 3000.0           # the soak's recovery limit
BOOT_SEED, BOOT_REPS = 43, 4000
ARMS = ("off", "on")
# (backend, seed, arm, profile, cycles), arms alternating; GNS loss cannot be seeded, so its "seed" is a repetition id.
BATCHES = ([(b, s, a, "harsh", 100) for b, seeds in (("udp", (4301, 4302, 4303)), ("gns", (1, 2, 3))) for s in seeds for a in ARMS]
           + [(b, s, a, "latency", 50) for b, s in (("udp", 4304), ("gns", 4)) for a in ARMS])


def name(b, s, a, p) -> str:
    return f"{b}-{s}-{a}-{p}"


def newcombe(k1: int, n1: int, k0: int, n0: int) -> list[float] | None:
    """95% hybrid score interval for p1 - p0 (Newcombe 1998, method 10), from the two Wilson intervals."""
    w1, w0 = rc.wilson(k1, n1), rc.wilson(k0, n0)
    if w1 is None or w0 is None:
        return None
    p1, p0 = k1 / n1, k0 / n0
    d = p1 - p0
    return [round(d - math.sqrt((p1 - w1[0]) ** 2 + (w0[1] - p0) ** 2), 4),
            round(d + math.sqrt((w1[1] - p1) ** 2 + (p0 - w0[0]) ** 2), 4)]


def quantile(xs: list[float], q: int) -> float:
    s = sorted(xs)
    return s[min(len(s) - 1, int(q / 100 * len(s)))]


def boot_diff(on: list[float], off: list[float], q: int) -> dict:
    """Difference of nearest-rank quantiles on - off with a seeded percentile bootstrap (95%)."""
    if not on or not off:
        return {"diff": None, "ci95": None}
    rng = random.Random(BOOT_SEED)
    diffs = sorted(quantile(rng.choices(on, k=len(on)), q) - quantile(rng.choices(off, k=len(off)), q)
                   for _ in range(BOOT_REPS))
    return {"diff": round(quantile(on, q) - quantile(off, q), 1),
            "ci95": [round(diffs[int(0.025 * BOOT_REPS)], 1), round(diffs[int(0.975 * BOOT_REPS) - 1], 1)]}


def direction(ci: list[float] | None) -> str:
    if ci is None:
        return "no_data"
    return "on_better" if ci[1] < 0 else "on_worse" if ci[0] > 0 else "no_difference_shown"


def total_ms(c: dict) -> float:
    return c["connected"] if c["connected"] >= 0 else rc.CYCLE_LIMIT_MS  # never connected: censored at the limit


FIRST_REQUEST_MS = (-2.0, 50.0)  # the client sends it in the update that authenticates it (log time vs auth)
STALL_MS = 5000.0                # the next cycle's stall: no request can be sent during it


def window_end(c: dict, next_log_ms: float | None) -> float:
    """The latest log time a request of this reconnect can have: Connected, or (never connected) the cycle limit,
    never past the start of the next cycle's stall (Codex: a later cycle's request must not stand in)."""
    end = c["log_ms"] + (c["connected"] if c["connected"] >= 0 else rc.CYCLE_LIMIT_MS) + 1
    return end if next_log_ms is None else min(end, next_log_ms - STALL_MS)


def classify(c: dict, reqs: list[float], next_log_ms: float | None) -> dict:
    """EXP-043's own D1: the reconnect's first request must be present at authentication, or D1 is unknown."""
    if c["auth"] < 0:
        return {"eligible": False}
    a = c["log_ms"] + c["auth"]
    first = [t for t in reqs if a + FIRST_REQUEST_MS[0] <= t <= min(a + FIRST_REQUEST_MS[1], window_end(c, next_log_ms))]
    if not first:
        return {"eligible": False}  # missing evidence: unknown, never "no drop"
    t1 = first[0]
    drops = [c["log_ms"] + d for d in c["host_drops"]]
    return {"eligible": True, "d1": any(rr.D1_WINDOW_MS[0] <= d - t1 <= rr.D1_WINDOW_MS[1] for d in drops)}


def load(d: Path) -> list[dict]:
    cycles = rc.load_batch(d)
    log = d / "client.log"
    # Only reconnect requests actually sent: a fresh rejoin between cycles logs reconnecting: false.
    reqs = sorted(e["t_ms"] for e in (rc.read_log(log) if log.exists() else [])
                  if e.get("evt") == "connect_request" and e.get("sent") is True and e.get("reconnecting") is True)
    for i, c in enumerate(cycles):
        nxt = cycles[i + 1]["log_ms"] if i + 1 < len(cycles) else None
        c["reorder"] = classify(c, reqs, nxt) if "host_drops" in c else {"eligible": False}
        c["requests"] = (None if c["auth"] < 0 else
                         sum(c["log_ms"] + c["auth"] - 2 <= t <= window_end(c, nxt) for t in reqs))
    return cycles


def summarize(cycles: list[dict]) -> dict:
    rec = [c for c in cycles if c["outcome"] != "no_reconnect"]
    totals = [total_ms(c) for c in rec]
    over = sum(t > LIMIT_MS for t in totals)
    sess = [c["connected"] - c["auth"] for c in rec if c["connected"] >= 0 and c["auth"] >= 0]
    reqs = [c["requests"] for c in rec if c["requests"] is not None]
    el = [c["reorder"] for c in rec if c["reorder"].get("eligible")]
    return {"cycles": len(cycles), "reconnects": len(rec), "no_reconnect": len(cycles) - len(rec),
            "timeouts": sum(c["outcome"] == "timeout" for c in rec), "failed": sum(c["outcome"] == "failed" for c in rec),
            "gate_timeouts": sum(c.get("gate_timeouts", 0) for c in rec),
            "total_ms": {f"p{q}": rc.pct(totals, q) for q in (50, 90, 95, 99)},
            "over_limit": {"k": over, "n": len(totals), "p": round(over / len(totals), 4) if totals else None,
                           "wilson95": rc.wilson(over, len(totals))},
            "session_ms": {"p50": rc.pct(sess, 50), "p95": rc.pct(sess, 95)},
            "requests_per_reconnect": {"mean": round(sum(reqs) / len(reqs), 3) if reqs else None,
                                       "dist": {str(k): reqs.count(k) for k in sorted(set(reqs))}},
            "d1": {"k": sum(x["d1"] for x in el), "n": len(el)},
            "defects": sum(c["reorder"].get("d1", False) and c.get("gate_timeouts", 0) == 0 for c in rec
                           if c.get("arm") == "on"),
            # Authenticated reconnects whose first request is not in the log: D1 unknown, never "no defect" (Codex).
            "d1_unknown": sum(c["auth"] >= 0 and not c["reorder"].get("eligible") for c in rec)}


def completeness(out: Path, raw: dict[str, list[dict]]) -> list[str]:
    run_path = out / "run.json"
    if not run_path.exists():
        return ["no run.json"]
    run = json.loads(run_path.read_text(encoding="utf-8"))
    problems = []
    want = {name(b, s, a, p): (a, p, n) for b, s, a, p, n in BATCHES}
    if sorted(raw) != sorted(want):
        problems.append(f"batches {sorted(raw)} != {sorted(want)}")
    for nm, cs in raw.items():
        if nm not in want:
            continue
        a, p, n = want[nm]
        if [c["cycle"] for c in cs] != list(range(n)):
            problems.append(f"{nm}: cycles not 0..{n - 1} ({len(cs)} recorded)")
        if any(c.get("arm") != a or c.get("profile") != p for c in cs):
            problems.append(f"{nm}: arm/profile not recorded as {a}/{p}")
        if any("host_drops" not in c for c in cs):
            problems.append(f"{nm}: no host trace")
        if not (out / nm / "client.log").exists():
            problems.append(f"{nm}: no client log")
        if run.get("exit_codes", {}).get(nm) != 0:
            problems.append(f"{nm}: exit {run.get('exit_codes', {}).get(nm)}")
    return problems


def judge(out: Path) -> dict:
    raw = {d.name: load(d) for d in sorted(p for p in out.iterdir() if p.is_dir())}
    problems = completeness(out, raw)
    v = {"complete": not problems, "incomplete_because": problems,
         "batches": {k: summarize(cs) for k, cs in raw.items()}, "pooled": {}, "rules": {}}
    for backend in ("udp", "gns"):
        for profile in ("harsh", "latency"):
            arms = {a: [c for k, cs in raw.items() if k.startswith(backend + "-") and k.endswith(f"-{a}-{profile}") for c in cs]
                    for a in ARMS}
            if not all(arms.values()):
                continue
            key = f"{backend}-{profile}"
            v["pooled"][key] = {a: summarize(cs) for a, cs in arms.items()}
            on_t = [total_ms(c) for c in arms["on"] if c["outcome"] != "no_reconnect"]
            off_t = [total_ms(c) for c in arms["off"] if c["outcome"] != "no_reconnect"]
            r = {"O3_p50": boot_diff(on_t, off_t, 50)}
            if profile == "harsh":
                po = v["pooled"][key]
                o1 = newcombe(po["on"]["over_limit"]["k"], po["on"]["over_limit"]["n"],
                              po["off"]["over_limit"]["k"], po["off"]["over_limit"]["n"])
                hom = {a: {m: rc.homogeneity_p([(v["batches"][k]["over_limit" if m == "over" else "d1"]["k"],
                                                  v["batches"][k]["over_limit" if m == "over" else "d1"]["n"])
                                                 for k in raw if k.startswith(backend + "-") and k.endswith(f"-{a}-harsh")])
                           for m in ("over", "d1")} for a in ARMS}
                hetero = any(h["over"] is not None and h["over"] < rc.HOMOGENEITY_P for h in hom.values())
                r["homogeneity_p"] = hom
                r["O1_over_limit"] = {"diff_ci95": o1, "verdict": "incomplete" if problems else
                                      "heterogeneous" if hetero else direction(o1)}
                for q in (95, 99):
                    b = boot_diff(on_t, off_t, q)
                    r[f"O2_p{q}"] = {**b, "verdict": "incomplete" if problems else direction(b["ci95"])}
            v["rules"][key] = r
    defects = sum(s["defects"] for s in v["batches"].values())
    unknown = sum(s["d1_unknown"] for k, s in v["batches"].items() if "-on-" in k)
    v["rules"]["O4_defects"] = defects
    v["rules"]["O4_unassessable_reconnects"] = unknown  # in arm on: any > 0 blocks a recommendation
    harsh = [v["rules"].get(f"{b}-harsh") for b in ("udp", "gns")]
    harsh = [h for h in harsh if h]
    verdicts = [h[o]["verdict"] for h in harsh for o in ("O1_over_limit", "O2_p95", "O2_p99")]
    better = any(h["O1_over_limit"]["verdict"] == "on_better" or h["O2_p95"]["verdict"] == "on_better" for h in harsh)
    worse = any(x == "on_worse" for x in verdicts)
    v["rules"]["recommendation"] = ("incomplete" if problems else
                                    "o4_unassessable" if unknown else
                                    "recommend" if better and not worse and defects == 0 else "do_not_recommend")
    return v


def main(argv: list[str], batches=BATCHES, judge=judge, prefix: str = "ki35-ab", doc: str = __doc__) -> int:
    """Runs `batches` in order into a fresh evidence directory and judges it (EXP-043; EXP-044 reuses it)."""
    if argv[:1] == ["--analyze"] and len(argv) == 2:
        print(json.dumps(judge(Path(argv[1])), indent=2))
        return 0
    if not argv or not Path(argv[0]).is_file():
        print(doc)
        return 2
    git = lambda *a: subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    commit = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain", "--", ".", ":(exclude)evidence"))
    started = datetime.datetime.now()
    out = ROOT / "evidence" / "logs" / f"{prefix}-{started:%Y-%m-%d-%H%M%S}-{commit[:7]}{'-dirty' if dirty else ''}"
    out.mkdir(parents=True)  # raises if it exists: evidence is never overwritten
    exe = str(Path(argv[0]).resolve())
    codes = {}
    for b, s, a, p, n in batches:
        d = out / name(b, s, a, p)
        d.mkdir()
        codes[d.name] = run_bounded([exe, b, str(n), str(s), str(d / "client.log"), a, p], d / "cycles.jsonl", 7200)
        print(f"{d.name}: exit {codes[d.name]}, {len(rc.load_cycles(d))} cycles", flush=True)
        (out / "run.json").write_text(json.dumps({"started": started.isoformat(timespec="seconds"), "commit": commit, "dirty": dirty,
                                                  "batches": batches, "exit_codes": codes}, indent=2) + "\n", encoding="utf-8")
    verdict = judge(out)
    (out / "verdict.json").write_text(json.dumps(verdict, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(verdict["rules"], indent=2))
    print(f"recorded: {out.relative_to(ROOT).as_posix()}")
    return 0 if all(c == 0 for c in codes.values()) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
