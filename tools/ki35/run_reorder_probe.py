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
# Fixed in the predeclaration as amended before any run (tools/ki35/model.py --reorder with DTLS replay suppression,
# 1,000,000 samples, seed 42; value +/- 0.002, never below 0).
H = {"d1": 0.4886, "first": 0.3067, "first_given_d1": 0.0, "first_given_not_d1": 0.5997}
IN_ORDER_D1 = 0.1273
MODEL = {k: [round(max(0.0, v - 0.002), 4), round(v + 0.002, 4)] for k, v in H.items()}
UNKNOWN_MAX = 0.10                    # above: the first-request rules (R2, R3) are not assessable
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
    if "host_drops" not in c:  # untraced (pre-EXP-042 probe): never counted as no drop; judge() marks the run incomplete
        return {"eligible": False, "untraced": True}
    t1 = first_request_ms(c, sent)
    if t1 is None:
        return {"eligible": False}
    drops = [c["log_ms"] + d for d in c["host_drops"]]
    d1 = any(D1_WINDOW_MS[0] <= d - t1 <= D1_WINDOW_MS[1] for d in drops)
    att = c["attribution"]
    out = {"eligible": True, "d1": d1, "drops": len(drops)}
    if c["connected"] < 0:      # authenticated but the session never completed: a known first = false (Codex)
        out["first"] = False
    elif att["attributed"]:
        out["first"] = att["index"] == 0
    # otherwise the completing request is unknown (no accept, no candidate or ambiguous): counted, never guessed
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
    at = [x for x in el if "first" in x]
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
    unknown = sum("first" not in x for x in el)
    return {"cycles": len(cycles), "eligible": len(el), "first_unknown": unknown,
            "first_unknown_share": round(unknown / len(el), 4) if el else None,
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
    # Without the host trace there is no D1 measurement: a run by a pre-EXP-042 probe must not read as "no drops" (Codex).
    untraced = sorted(k for k, cs in raw.items() if any("host_drops" not in c or "host_auth" not in c for c in cs))
    if untraced:
        problems.append(f"no EXP-042 host trace in {untraced}")
    v = {"model_h": H, "model_intervals": MODEL, "in_order_d1": IN_ORDER_D1, "deltas": DELTAS, "complete": not problems,
         "incomplete_because": problems, "batches": per_batch,
         "pooled": {b: summarize(cs) for b, cs in pooled.items() if cs}, "rules": {}}
    hom = {b: {name: rc.homogeneity_p([(x["proportions"][name]["k"], x["proportions"][name]["n"])
                                       for k, x in per_batch.items() if k.startswith(b)]) for name in H}
           for b in v["pooled"]}
    heterogeneous = lambda b, name: hom[b][name] is not None and hom[b][name] < rc.HOMOGENEITY_P
    if "udp" in v["pooled"]:
        u, rules = v["pooled"]["udp"], {}
        not_assessable = (u["first_unknown_share"] or 0) > UNKNOWN_MAX
        for name in H:
            pr = u["proportions"][name]
            r = {"k": pr["k"], "n": pr["n"], "wilson90": pr["wilson90"], "wilson95": pr["wilson95"],
                 "homogeneity_p": hom["udp"][name]}
            if problems:
                r["verdict"] = "incomplete"
            elif name != "d1" and not_assessable:  # the attribution gate comes before any verdict, pooled or per batch
                r["verdict"] = "not_assessable"
            elif heterogeneous("udp", name):
                r["verdict"] = "heterogeneous"  # predeclared: per batch only, never pooled
                r["per_batch"] = {k: ("not_assessable" if name != "d1" and (x["first_unknown_share"] or 0) > UNKNOWN_MAX
                                      else rc.tost(x["proportions"][name]["k"], x["proportions"][name]["n"], MODEL[name], 0.08))
                                  for k, x in per_batch.items() if k.startswith("udp")}
            else:
                r["verdict"] = rc.tost(pr["k"], pr["n"], MODEL[name], 0.08)
                r["sensitivity"] = {str(dl): rc.tost(pr["k"], pr["n"], MODEL[name], dl) for dl in DELTAS}
            rules[name] = r
        ci = u["proportions"]["d1"]["wilson95"]
        rules["in_order_contrast"] = ("no_data" if ci is None else
                                      "different" if not (ci[0] <= IN_ORDER_D1 <= ci[1]) else "not_different")
        verdicts = [rules[n]["verdict"] for n in H]
        if problems:
            rules["H"] = "incomplete"
        elif "different" in verdicts:  # refutation needs an actual "different" test result (Codex)
            rules["H"] = "refuted"
        elif all(x == "equivalent" for x in verdicts) and rules["in_order_contrast"] == "different":
            rules["H"] = "supported"
        else:
            rules["H"] = "inconclusive"
        v["rules"]["udp"] = rules
    if "gns" in v["pooled"] and "udp" in v["pooled"]:
        g, u = v["pooled"]["gns"]["proportions"], v["pooled"]["udp"]["proportions"]
        gd, ud = g["d1"]["wilson95"], u["d1"]["wilson95"]
        a, b = g["first_given_d1"]["wilson95"], g["first_given_not_d1"]["wilson95"]
        hetero = [f"{bk}:{n}" for bk, n in (("udp", "d1"), ("gns", "d1"), ("gns", "first_given_d1"),
                                             ("gns", "first_given_not_d1")) if heterogeneous(bk, n)]
        if problems:
            v["rules"]["gns"] = "incomplete"
        elif hetero:
            v["rules"]["gns"] = {"verdict": "heterogeneous", "inputs": hetero}  # no pooled directional judgement
        else:
            v["rules"]["gns"] = {"d1_below_udp": bool(gd and ud and gd[1] < ud[0]),
                                 "dropped_first_rarely_completes": bool(a and b and a[1] < b[0])}
    v["homogeneity_p"] = hom
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
