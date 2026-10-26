"""EXP-042 runner: does the first session request overtake the client's binding? Runs ki35_reconnect_probe in the
predeclared batches, records everything immutably, and applies the rules predeclared in docs/soak-testing.md
("Predeclared experiment KI-35: does the first session request overtake the binding?").
  python -I tools/ki35/run_reorder_probe.py build-gns/Release/ki35_reconnect_probe.exe [cycles_per_batch]
Writes evidence/logs/ki35-reorder-<time>-<commit7>[-dirty]/ (per-batch cycles and client logs, run.json,
verdict.json); never overwrites. --analyze DIR re-reads a recorded directory without running anything.
"""
from __future__ import annotations

import datetime
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_reconnect_probe as rc  # noqa: E402  (shared: loading, attribution, Wilson, TOST, homogeneity, completeness)
from bounded import run_bounded  # noqa: E402

ROOT = rc.ROOT
# Fixed in the predeclaration (tools/ki35/model.py --reorder, 1,000,000 samples, seed 42; value +/- 0.002).
H = {"d1": 0.4888, "first": 0.3183, "first_given_d1": 0.0208, "first_given_not_d1": 0.6028}
IN_ORDER_D1 = 0.1274
MODEL = {k: [round(v - 0.002, 4), round(v + 0.002, 4)] for k, v in H.items()}
DELTAS = (0.05, 0.08, 0.10)           # 0.08 primary; the others are sensitivity only
D1_WINDOW_MS = (70.0, 130.0)          # host drop after the first request's send (one way 80-120 ms)
BATCHES = [("udp", 4201), ("udp", 4202), ("udp", 4203), ("gns", 1), ("gns", 2), ("gns", 3)]


def first_request_ms(c: dict, sent: list[float]) -> float | None:
    """Log time of the first ConnectRequest actually sent after the client authenticated, in this cycle."""
    if c["auth"] < 0:
        return None
    lo, hi = c["log_ms"] + c["auth"] - 2, c["log_ms"] + rc.CYCLE_LIMIT_MS
    return next((t for t in sorted(sent) if lo <= t <= hi), None)


def classify(c: dict, sent: list[float]) -> dict:
    t1 = first_request_ms(c, sent)
    if t1 is None:
        return {"eligible": False}
    drops = [c["log_ms"] + d for d in c.get("host_drops", [])]
    d1 = any(D1_WINDOW_MS[0] <= d - t1 <= D1_WINDOW_MS[1] for d in drops)
    att = c["attribution"]
    out = {"eligible": True, "d1": d1, "drops": len(drops), "attributed": att["attributed"]}
    if att["attributed"]:
        out["first"] = att["index"] == 0
    if d1 and c.get("host_auth", -1) >= 0:  # how long the binding trailed the dropped request at the host
        first_drop = min(d for d in drops if D1_WINDOW_MS[0] <= d - t1 <= D1_WINDOW_MS[1])
        out["binding_after_drop_ms"] = round(c["log_ms"] + c["host_auth"] - first_drop, 1)
    return out


def load(d: Path) -> list[dict]:
    cycles = rc.load_batch(d)
    log = d / "client.log"
    sent = [e["t_ms"] for e in (rc.read_log(log) if log.exists() else [])
            if e.get("evt") == "connect_request" and e.get("sent") is True]
    for c in cycles:
        c["reorder"] = classify(c, sent)
    return cycles


def series(cycles: list[dict]) -> dict[str, list[int]]:
    el = [c["reorder"] for c in cycles if c["reorder"]["eligible"]]
    at = [x for x in el if x["attributed"]]
    return {"d1": [int(x["d1"]) for x in el], "first": [int(x["first"]) for x in at],
            "first_given_d1": [int(x["first"]) for x in at if x["d1"]],
            "first_given_not_d1": [int(x["first"]) for x in at if not x["d1"]]}


def summarize(cycles: list[dict]) -> dict:
    s = series(cycles)
    el = [c["reorder"] for c in cycles if c["reorder"]["eligible"]]
    lag = sorted(x["binding_after_drop_ms"] for x in el if "binding_after_drop_ms" in x)
    drops = {}
    for x in el:
        drops[x["drops"]] = drops.get(x["drops"], 0) + 1
    return {"cycles": len(cycles), "eligible": len(el), "unattributed": sum(not x["attributed"] for x in el),
            "proportions": {k: {"k": sum(v), "n": len(v), "p": round(sum(v) / len(v), 4) if v else None,
                                "wilson90": rc.wilson(sum(v), len(v), rc.Z90), "wilson95": rc.wilson(sum(v), len(v)),
                                "lag1": rc.lag1(v)} for k, v in s.items()},
            "drops_per_reconnect": dict(sorted(drops.items())),
            "binding_after_drop_ms": {"n": len(lag), "p50": rc.pct(lag, 50), "p95": rc.pct(lag, 95)}}


def judge(out: Path) -> dict:
    raw, per_batch, pooled = {}, {}, {"udp": [], "gns": []}
    for d in sorted(p for p in out.iterdir() if p.is_dir()):
        cycles = load(d)
        raw[d.name] = cycles
        per_batch[d.name] = summarize(cycles)
        pooled[d.name.split("-")[0]] += cycles
    problems = rc.completeness(out, raw, BATCHES)
    v = {"model_h": H, "model_intervals": MODEL, "in_order_d1": IN_ORDER_D1, "deltas": DELTAS, "complete": not problems,
         "incomplete_because": problems, "batches": per_batch,
         "pooled": {b: summarize(cs) for b, cs in pooled.items() if cs}, "rules": {}}
    if "udp" in v["pooled"]:
        u, rules = v["pooled"]["udp"], {}
        for name in H:
            pr = u["proportions"][name]
            hp = rc.homogeneity_p([(b["proportions"][name]["k"], b["proportions"][name]["n"])
                                   for k, b in per_batch.items() if k.startswith("udp")])
            r = {"k": pr["k"], "n": pr["n"], "wilson90": pr["wilson90"], "wilson95": pr["wilson95"], "homogeneity_p": hp}
            if problems:
                r["verdict"] = "incomplete"
            elif hp is not None and hp < rc.HOMOGENEITY_P:
                r["verdict"] = "heterogeneous"
            else:
                r["verdict"] = rc.tost(pr["k"], pr["n"], MODEL[name], 0.08)
                r["sensitivity"] = {str(dl): rc.tost(pr["k"], pr["n"], MODEL[name], dl) for dl in DELTAS}
            rules[name] = r
        ci = u["proportions"]["d1"]["wilson95"]
        rules["d1_differs_from_in_order"] = bool(ci and not (ci[0] <= IN_ORDER_D1 <= ci[1]))
        verdicts = [rules[n]["verdict"] for n in H]
        if problems:
            rules["H"] = "incomplete"
        elif "different" in verdicts or not rules["d1_differs_from_in_order"]:
            rules["H"] = "refuted"
        elif all(x == "equivalent" for x in verdicts):
            rules["H"] = "supported"
        else:
            rules["H"] = "inconclusive"
        v["rules"]["udp"] = rules
    if "gns" in v["pooled"] and "udp" in v["pooled"]:
        g, u = v["pooled"]["gns"]["proportions"], v["pooled"]["udp"]["proportions"]
        gd, ud = g["d1"]["wilson95"], u["d1"]["wilson95"]
        a, b = g["first_given_d1"]["wilson95"], g["first_given_not_d1"]["wilson95"]
        v["rules"]["gns"] = "incomplete" if problems else {
            "d1_below_udp": bool(gd and ud and gd[1] < ud[0]),
            "dropped_first_rarely_completes": bool(a and b and a[1] < b[0])}
    return v


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
    out = ROOT / "evidence" / "logs" / f"ki35-reorder-{started:%Y-%m-%d-%H%M%S}-{commit[:7]}{'-dirty' if dirty else ''}"
    out.mkdir(parents=True)  # raises if it exists: evidence is never overwritten
    exe = str(Path(argv[0]).resolve())
    codes = {}
    for backend, seed in BATCHES:
        d = out / f"{backend}-{seed}"
        d.mkdir()
        codes[d.name] = run_bounded([exe, backend, cycles, str(seed), str(d / "client.log")], d / "cycles.jsonl", 7200)
        print(f"{d.name}: exit {codes[d.name]}, {len(rc.load_cycles(d))} cycles", flush=True)
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
