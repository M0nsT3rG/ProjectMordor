"""EXP-041 runner (FB-25-05..08): runs ki35_reconnect_probe in the predeclared batches, records everything immutably,
and applies the rules predeclared in docs/soak-testing.md ("Predeclared experiment KI-35: phases of each stall
reconnect").
  python -I tools/ki35/run_reconnect_probe.py build-gns/Release/ki35_reconnect_probe.exe [cycles_per_batch]
Writes evidence/logs/ki35-reconnect-<time>-<commit7>[-dirty]/ (per-batch cycles and client logs, run.json,
verdict.json); never overwrites. --analyze DIR re-reads a recorded directory without running anything.
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

# Fixed in the predeclaration; never tuned to results. Models: tools/ki35/model.py proportions(), unchanged.
MODEL = {"dtls_no_timer": [0.3771, 0.3944], "session_first": [0.6141, 0.628], "session_first_2leg": [0.7225, 0.7334],
         "gns_first_cycle": [0.522, 0.5378]}
DELTA = 0.08                      # TOST equivalence margin around the model interval
HOMOGENEITY_P = 0.01              # below: batches heterogeneous, no pooled verdict
RTT_WINDOW_MS = (140.0, 260.0)    # harsh round trip 160-240 ms +/- 20 ms of processing (FB-25-08)
UNATTRIBUTED_MAX = 0.10           # above: session_first not assessable
GNS_FIRST_CYCLE_MS = 800.0        # inner Connected within the first 500 ms GNS cycle, from Reconnecting (EXP-036)
RECOVERY_LIMIT_MS = 3000.0        # the soak's recovery limit (tools/soak/analyze.py)
CYCLE_LIMIT_MS = 20000.0          # the probe's per-cycle limit (kCycleLimitUs)
BATCHES = [("udp", 4101), ("udp", 4102), ("udp", 4103), ("gns", 1), ("gns", 2), ("gns", 3)]
Z95, Z90 = 1.959964, 1.644854


def wilson(k: int, n: int, z: float = Z95) -> list[float] | None:
    if n == 0:
        return None
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return [round(c - h, 4), round(c + h, 4)]


def pct(xs: list[float], p: int) -> float | None:
    s = sorted(xs)
    return round(s[min(len(s) - 1, int(p / 100 * len(s)))], 1) if s else None


def tost(k: int, n: int, model: list[float]) -> str:
    """equivalent: the 90% interval inside [lo - delta, hi + delta]; different: the 95% interval misses [lo, hi]."""
    ci90, ci95 = wilson(k, n, Z90), wilson(k, n, Z95)
    if ci90 is None:
        return "no_data"
    if model[0] - DELTA <= ci90[0] and ci90[1] <= model[1] + DELTA:
        return "equivalent"
    if ci95[1] < model[0] or ci95[0] > model[1]:
        return "different"
    return "inconclusive"


def homogeneity_p(counts: list[tuple[int, int]]) -> float | None:
    """Chi-square test of equal proportions across batches. Exact survival function for df = 2 (3 batches)."""
    counts = [(k, n) for k, n in counts if n > 0]
    if len(counts) < 2:
        return None
    K, N = sum(k for k, _ in counts), sum(n for _, n in counts)
    p = K / N
    if p in (0.0, 1.0):
        return 1.0
    x2 = sum((k - n * p) ** 2 / (n * p) + ((n - k) - n * (1 - p)) ** 2 / (n * (1 - p)) for k, n in counts)
    df = len(counts) - 1
    if df == 2:
        return round(math.exp(-x2 / 2), 6)
    if df == 1:  # survival of chi-square(1) = erfc(sqrt(x/2))
        return round(math.erfc(math.sqrt(x2 / 2)), 6)
    raise ValueError("predeclared for 3 batches per backend")


def lag1(xs: list[int]) -> float | None:
    if len(xs) < 3:
        return None
    m = sum(xs) / len(xs)
    den = sum((x - m) ** 2 for x in xs)
    return None if den == 0 else round(sum((a - m) * (b - m) for a, b in zip(xs, xs[1:])) / den, 4)


def load_cycles(d: Path) -> list[dict]:
    lines = (d / "cycles.jsonl").read_text(encoding="utf-8").splitlines() if (d / "cycles.jsonl").exists() else []
    return [json.loads(l) for l in lines if l.strip().endswith("}")]  # a hung probe's last line may be cut


def attribute(cycle: dict, sent: list[float], accepts: list[float]) -> dict:
    """FB-25-08: which sent ConnectRequest did the accept complete? By timestamps only (client log, ms)."""
    if cycle["auth"] < 0:
        return {"attributed": False, "why": "not_authenticated"}
    if cycle["connected"] < 0:  # authenticated but never Connected: stays in the denominator, unattributed (Codex)
        return {"attributed": False, "why": "no_session"}
    # The client logs connect_accept_received only for the accept that completes the reconnect: one per reconnect.
    # Its log time can trail `connected` (sampled before the update) by several ms, so the cycle's own limit bounds
    # it, not connected + 1 ms (Codex).
    lo, hi = cycle["log_ms"] + cycle["auth"] - 2, cycle["log_ms"] + CYCLE_LIMIT_MS
    acc = next((a for a in accepts if lo <= a <= hi), None)
    if acc is None:
        return {"attributed": False, "why": "no_accept"}
    reqs = sorted(r for r in sent if lo <= r <= acc)
    cands = [i for i, r in enumerate(reqs) if RTT_WINDOW_MS[0] <= acc - r <= RTT_WINDOW_MS[1]]
    if len(cands) != 1:
        return {"attributed": False, "why": "ambiguous" if cands else "no_candidate", "requests": len(reqs)}
    return {"attributed": True, "index": cands[0], "requests": len(reqs)}


def load_batch(d: Path) -> list[dict]:
    cycles = load_cycles(d)
    log = d / "client.log"
    evts = [json.loads(l) for l in log.read_text(encoding="utf-8").splitlines() if l.strip()] if log.exists() else []
    sent = [e["t_ms"] for e in evts if e.get("evt") == "connect_request" and e.get("sent") is True]
    accepts = [e["t_ms"] for e in evts if e.get("evt") == "connect_accept_received"]
    for c in cycles:  # every cycle, whatever its outcome: eligibility comes from the phases reached (Codex)
        c["attribution"] = attribute(c, sent, accepts) if log.exists() else {"attributed": False, "why": "no_log"}
    return cycles


PHASES = ("notice", "establish", "dtls", "session")


def phases(c: dict) -> dict:
    """Each phase only once both of its ends were reached (-1 = not reached), so a failed cycle keeps what it did."""
    ends = {"notice": (0.0, c["notice"]), "establish": (c["notice"], c["first_hello"]), "dtls": (c["first_hello"], c["auth"]),
            "session": (c["auth"], c["connected"]), "total": (0.0, c["connected"]), "first_snapshot": (0.0, c["first_snapshot"])}
    return {k: (b - a) for k, (a, b) in ends.items() if a >= 0 and b >= 0}


def outcomes(cycles: list[dict], backend: str) -> dict[str, list[int]]:
    """Per-cycle 0/1 outcome series (in order) for each proportion: eligibility per phase, never on overall success."""
    out = {"dtls_no_timer": [int(c["auth"] >= 0 and c["timers"] == 0) for c in cycles if c["first_hello"] >= 0],
           "session_first": [int(c["attribution"]["index"] == 0) for c in cycles if c["attribution"]["attributed"]]}
    if backend == "gns":
        out["gns_first_cycle"] = [int(c["inner"] >= 0 and c["inner"] - c["notice"] < GNS_FIRST_CYCLE_MS)
                                  for c in cycles if c["notice"] >= 0]
    return out


def summarize(cycles: list[dict], backend: str) -> dict:
    ok = [c for c in cycles if c["outcome"] == "ok"]
    reconnected = [c for c in cycles if c["connected"] >= 0]
    attempts = [c for c in cycles if c["outcome"] != "no_reconnect"]
    ph = {k: [phases(c)[k] for c in cycles if k in phases(c)] for k in (*PHASES, "total", "first_snapshot")}
    lat = {k: {"n": len(v), **{f"p{p}": pct(v, p) for p in (50, 90, 95, 99)}, "max": max(v) if v else None}
           for k, v in ph.items()}
    series = outcomes(cycles, backend)
    props = {k: {"k": sum(v), "n": len(v), "p": round(sum(v) / len(v), 4) if v else None, "wilson95": wilson(sum(v), len(v)),
                 "wilson90": wilson(sum(v), len(v), Z90), "lag1": lag1(v)} for k, v in series.items()}
    with_session = [c for c in cycles if c["auth"] >= 0]  # failures after authentication included (Codex)
    unattributed = sum(not c["attribution"]["attributed"] for c in with_session)
    complete = [c for c in reconnected if all(x in phases(c) for x in PHASES)]
    slow = sorted(complete, key=lambda c: c["connected"])[-max(1, len(complete) // 10):] if complete else []
    largest = {}
    for c in slow:
        p = phases(c)
        k = max(PHASES, key=lambda x: p[x])
        largest[k] = largest.get(k, 0) + 1
    over = sum(c["connected"] < 0 or c["connected"] > RECOVERY_LIMIT_MS for c in attempts)  # recovery = Connected
    cookie = [c["cookie_hello"] - c["first_hello"] for c in cycles if c["cookie_hello"] >= 0 and c["first_hello"] >= 0]
    return {"cycles": len(cycles), "ok": len(ok), "reconnected": len(reconnected),
            "no_snapshot": sum(c["outcome"] == "no_snapshot" for c in cycles),
            "timeouts": sum(c["outcome"] == "timeout" for c in cycles),
            "failed": sum(c["outcome"] == "failed" for c in cycles),
            "no_reconnect": sum(c["outcome"] == "no_reconnect" for c in cycles),
            "latency_ms": lat, "proportions": props,
            "unattributed": {"k": unattributed, "n": len(with_session),
                             "share": round(unattributed / len(with_session), 4) if with_session else None},
            "over_recovery_limit": {"k": over, "n": len(attempts), "wilson95": wilson(over, len(attempts))},
            "slowest_10pct_largest_phase": largest,
            "cookie_ms": {"n": len(cookie), "p50": pct(cookie, 50), "p95": pct(cookie, 95)},
            "loop_ms_p50": pct([c["loop_ms"] for c in cycles], 50)}


def completeness(out: Path, per_batch_cycles: dict[str, list[dict]]) -> list[str]:
    """Why the recorded run is not the predeclared one (empty: complete). Verdicts are issued only when complete."""
    run_path = out / "run.json"
    if not run_path.exists():
        return ["no run.json"]
    run = json.loads(run_path.read_text(encoding="utf-8"))
    problems = []
    want = [f"{b}-{s}" for b, s in BATCHES]
    if sorted(per_batch_cycles) != sorted(want):
        problems.append(f"batches {sorted(per_batch_cycles)} != {sorted(want)}")
    if run.get("cycles_per_batch") != 100:
        problems.append(f"cycles_per_batch {run.get('cycles_per_batch')} != 100")
    for name, cs in per_batch_cycles.items():
        if [c["cycle"] for c in cs] != list(range(run.get("cycles_per_batch", -1))):
            problems.append(f"{name}: cycles not 0..{run.get('cycles_per_batch', 0) - 1} ({len(cs)} recorded)")
        if run.get("exit_codes", {}).get(name) != 0:
            problems.append(f"{name}: exit {run.get('exit_codes', {}).get(name)}")
    return problems


def judge(out: Path) -> dict:
    per_batch, pooled, raw = {}, {"udp": [], "gns": []}, {}
    for d in sorted(p for p in out.iterdir() if p.is_dir()):
        backend = d.name.split("-")[0]
        cycles = load_batch(d)
        raw[d.name] = cycles
        per_batch[d.name] = summarize(cycles, backend)
        pooled[backend] += cycles
    problems = completeness(out, raw)
    verdict = {"model": MODEL, "delta": DELTA, "rtt_window_ms": RTT_WINDOW_MS, "complete": not problems,
               "incomplete_because": problems, "batches": per_batch, "pooled": {}, "rules": {}}
    for backend, cycles in pooled.items():
        if not cycles:
            continue
        s = summarize(cycles, backend)
        verdict["pooled"][backend] = s
        rules = {}
        for name, pr in s["proportions"].items():
            bcounts = [(b["proportions"][name]["k"], b["proportions"][name]["n"]) for k, b in per_batch.items()
                       if k.startswith(backend) and name in b["proportions"]]
            hp = homogeneity_p(bcounts)
            r = {"k": pr["k"], "n": pr["n"], "wilson90": pr["wilson90"], "wilson95": pr["wilson95"], "homogeneity_p": hp}
            if problems:
                r["verdict"] = "incomplete"  # descriptive statistics only (Codex)
            elif hp is not None and hp < HOMOGENEITY_P:
                r["verdict"] = "heterogeneous"
            elif name == "session_first" and (s["unattributed"]["share"] or 0) > UNATTRIBUTED_MAX:
                r["verdict"] = "not_assessable"
            else:
                r["verdict"] = tost(pr["k"], pr["n"], MODEL[name])
                if name == "session_first":
                    r["verdict_2leg"] = tost(pr["k"], pr["n"], MODEL["session_first_2leg"])
            rules[name] = r
        dom = s["slowest_10pct_largest_phase"]
        n_slow = sum(dom.values())
        # Every phase largest in at least half of the slowest 10%: a 50/50 split names both (Codex).
        rules["tail_dominant_phases"] = sorted(k for k, v in dom.items() if n_slow and v / n_slow >= 0.5)
        verdict["rules"][backend] = rules
    return verdict


def main(argv: list[str]) -> int:
    if argv[:1] == ["--analyze"] and len(argv) == 2:
        print(json.dumps(judge(Path(argv[1])), indent=2))
        return 0
    if not argv or not Path(argv[0]).is_file():
        print(__doc__)
        return 2
    cycles = argv[1] if len(argv) > 1 else "100"
    git = lambda *a: subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    commit = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain", "--", ".", ":(exclude)evidence"))
    started = datetime.datetime.now()
    out = ROOT / "evidence" / "logs" / f"ki35-reconnect-{started:%Y-%m-%d-%H%M%S}-{commit[:7]}{'-dirty' if dirty else ''}"
    out.mkdir(parents=True)  # raises if it exists: evidence is never overwritten
    exe = str(Path(argv[0]).resolve())
    codes = {}
    for backend, seed in BATCHES:
        d = out / f"{backend}-{seed}"
        d.mkdir()
        codes[d.name] = run_bounded([exe, backend, cycles, str(seed), str(d / "client.log")], d / "cycles.jsonl", 7200)
        print(f"{d.name}: exit {codes[d.name]}, {len(load_cycles(d))} cycles", flush=True)
        (out / "run.json").write_text(json.dumps({"started": started.isoformat(timespec="seconds"), "commit": commit, "dirty": dirty,
                                                  "cycles_per_batch": int(cycles), "batches": BATCHES, "exit_codes": codes}, indent=2) + "\n",
                                      encoding="utf-8")
    verdict = judge(out)
    (out / "verdict.json").write_text(json.dumps(verdict, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(verdict["rules"], indent=2))
    print(f"recorded: {out.relative_to(ROOT).as_posix()}")
    return 0 if all(c == 0 for c in codes.values()) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
