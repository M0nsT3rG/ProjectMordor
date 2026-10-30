"""EXP-047 runner: intermediate DTLS back-off caps (off: 250/500/1000 ms; cap750: 250/500/750; cap500: 250/500/500),
host and client, under harsh and a slow benign profile. Runs ki35_reconnect_probe in the predeclared batches (arm order
rotating), records everything immutably, and applies the rules predeclared in docs/soak-testing.md ("Predeclared
experiment KI-35: intermediate DTLS back-off caps").
  python -I tools/ki35/run_cap_probe.py build-gns/Release/ki35_reconnect_probe.exe
Writes evidence/logs/ki35-caps-<time>-<commit7>[-dirty]/ (per-batch cycles and client logs, run.json, verdict.json);
never overwrites. --analyze DIR re-reads a recorded directory without running anything.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_ab_probe as ab  # noqa: E402  (Newcombe, bootstrap, censoring, the batch loop)
import run_reconnect_probe as rc  # noqa: E402  (Wilson, homogeneity, percentiles)
import run_timer_probe as tp  # noqa: E402  (EXP-044's trace invariants, CPU deltas, summaries)

CANDIDATES = ("cap750", "cap500")
ARMS = ("off",) + CANDIDATES
PROFILES = ("harsh", "slow")
BYTES_RATIO_MAX = 1.25  # G1: a candidate's mean bytes sent per reconnect (both sides) / off's, per backend and profile
ORDERS = (("off", "cap750", "cap500"), ("cap750", "cap500", "off"), ("cap500", "off", "cap750"))  # by repetition
# (backend, seed, arm, profile, cycles); GNS loss cannot be seeded, so its "seed" is a repetition id.
BATCHES = ([(b, s, a, "harsh", 100) for b, seeds in (("udp", (4701, 4702, 4703)), ("gns", (1, 2, 3)))
            for s, order in zip(seeds, ORDERS) for a in order]
           + [(b, s, a, "slow", 100) for b, s in (("udp", 4704), ("gns", 4)) for a in ORDERS[0]])
BYTE_FIELDS = ("client_tx_bytes", "host_tx_bytes", "client_rx_bytes", "host_rx_bytes", "client_resent", "host_resent")
EVENTS = tp.C2_EVENTS  # timeout, failed, no_reconnect: compared one by one (no offsetting)


def direction(ci: list[float] | None) -> str:
    return ab.direction(ci).replace("on_", "")  # better | worse | no_difference_shown | no_data


def traced(c: dict) -> bool:
    """EXP-044's invariants plus the byte and resend fields: non-negative integers; each side's bytes sent are at least
    its datagrams sent; resent datagrams never exceed datagrams sent."""
    if not tp.traced(c):
        return False
    if not all(isinstance(c[f], int) and not isinstance(c[f], bool) and c[f] >= 0 for f in BYTE_FIELDS):
        return False
    return all(c[f"{s}_tx_bytes"] >= c[f"{s}_tx"] and c[f"{s}_resent"] <= c[f"{s}_tx"] for s in ("client", "host"))


def mean(xs: list[float]) -> float | None:
    return round(sum(xs) / len(xs), 3) if xs else None


def summarize(cycles: list[dict]) -> dict:
    s = tp.summarize(cycles)
    rec = [c for c in cycles if c["outcome"] != "no_reconnect"]
    sent = [c["client_tx_bytes"] + c["host_tx_bytes"] for c in rec]
    s["bytes_sent"] = {"sum": sum(sent), "n": len(sent)}  # G1 uses these, unrounded
    s["bytes_sent_per_reconnect"] = mean(sent)
    s["per_side"] = {side: {"bytes_sent": mean([c[f"{side}_tx_bytes"] for c in rec]),
                            "bytes_received": mean([c[f"{side}_rx_bytes"] for c in rec]),
                            "datagrams": mean([c[f"{side}_tx"] for c in rec]),
                            "resent": mean([c[f"{side}_resent"] for c in rec]),
                            "timer_fires": mean([c["timers" if side == "client" else "host_timers"] for c in rec])}
                     for side in ("client", "host")}
    return s


def completeness(out: Path, raw: dict[str, list[dict]]) -> list[str]:
    run_path = out / "run.json"
    if not run_path.exists():
        return ["no run.json"]
    run = json.loads(run_path.read_text(encoding="utf-8"))
    problems = []
    want = {ab.name(b, s, a, p): (a, p, n) for b, s, a, p, n in BATCHES}
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
        if any(f not in c for c in cs for f in tp.TRACE_FIELDS + BYTE_FIELDS):
            problems.append(f"{nm}: no host trace, cost or byte fields")
        elif not all(traced(c) for c in cs):
            problems.append(f"{nm}: a reconnect without the trace evidence its phases imply")
        if not (out / nm / "client.log").exists():
            problems.append(f"{nm}: no client log")
        if run.get("exit_codes", {}).get(nm) != 0:
            problems.append(f"{nm}: exit {run.get('exit_codes', {}).get(nm)}")
    return problems


def judge(out: Path) -> dict:
    raw = {d.name: tp.load(d) for d in sorted(p for p in out.iterdir() if p.is_dir())}
    problems = completeness(out, raw)
    v = {"complete": not problems, "incomplete_because": problems,
         "batches": {k: summarize(cs) for k, cs in raw.items()} if not problems else {}, "pooled": {}, "rules": {}}
    if problems:  # fields may be missing: nothing is summarised, so nothing can be read as a result
        v["rules"]["reading"] = {c: "incomplete" for c in CANDIDATES}
        return v
    totals = lambda cs: [ab.total_ms(c) for c in cs if c["outcome"] != "no_reconnect"]
    dtls = lambda cs: [c["auth"] - c["first_hello"] for c in cs
                       if c["outcome"] != "no_reconnect" and c["auth"] >= 0 and c["first_hello"] >= 0]
    rules = {c: {} for c in CANDIDATES}
    for backend in ("udp", "gns"):
        hom = {}
        for a in ARMS:
            counts = [(v["batches"][k]["over_limit"]["k"], v["batches"][k]["over_limit"]["n"])
                      for k in raw if k.startswith(backend + "-") and k.endswith(f"-{a}-harsh")]
            # All 3 batches must be assessable, compared at full precision (EXP-044, Codex passes 1 and 6).
            hom[a] = rc.homogeneity_p(counts, exact=True) if len(counts) == 3 and all(n for _, n in counts) else None
        for profile in PROFILES:
            arms = {a: [c for k, cs in raw.items() if k.startswith(backend + "-") and k.endswith(f"-{a}-{profile}")
                        for c in cs] for a in ARMS}
            po = v["pooled"][f"{backend}-{profile}"] = {a: summarize(cs) for a, cs in arms.items()}
            off_t, off_d = totals(arms["off"]), dtls(arms["off"])
            for cand in CANDIDATES:
                c_t = totals(arms[cand])
                r = rules[cand].setdefault(f"{backend}-{profile}", {})
                r["O3_p50_total"] = ab.boot_diff(c_t, off_t, 50)
                r["O3_p50_dtls"] = ab.boot_diff(dtls(arms[cand]), off_d, 50)
                b_off, b_c = po["off"]["bytes_sent"], po[cand]["bytes_sent"]
                ok_bytes = b_off["sum"] > 0 and b_off["n"] > 0 and b_c["n"] > 0
                r["G1_bytes_ratio"] = round(b_c["sum"] / b_c["n"] / (b_off["sum"] / b_off["n"]), 4) if ok_bytes else None
                # Unrounded: sum_c / n_c <= 1.25 * sum_off / n_off.
                r["G1_ok"] = ok_bytes and b_c["sum"] * b_off["n"] <= BYTES_RATIO_MAX * b_off["sum"] * b_c["n"]
                ev = {a: {e: sum(c["outcome"] == e for c in arms[a]) for e in EVENTS} for a in ("off", cand)}
                r["G2_events"] = ev
                r["G2_ok"] = all(ev[cand][e] <= ev["off"][e] for e in EVENTS)
                if profile == "harsh":
                    o1 = ab.newcombe(po[cand]["over_limit"]["k"], po[cand]["over_limit"]["n"],
                                     po["off"]["over_limit"]["k"], po["off"]["over_limit"]["n"], exact=True)
                    h = {a: hom[a] for a in ("off", cand)}
                    hetero = any(x is not None and x < rc.HOMOGENEITY_P for x in h.values())
                    r["homogeneity_p"] = {a: x if x is None else round(x, 6) for a, x in h.items()}
                    r["O1_over_limit"] = {"diff_ci95": o1 and [round(x, 4) for x in o1],
                                          "verdict": "unassessable" if None in h.values() else
                                          "heterogeneous" if hetero else direction(o1)}
                    for q in (95, 99):
                        b = ab.boot_diff(c_t, off_t, q)
                        r[f"O2_p{q}"] = {**b, "verdict": direction(b["ci95"])}
                else:  # G3: the slow path's p95 and p50 must not be worse
                    p95 = ab.boot_diff(c_t, off_t, 95)
                    r["G3_p95"] = {**p95, "verdict": direction(p95["ci95"])}
                    r["G3_p50_verdict"] = direction(r["O3_p50_total"]["ci95"])
                    r["G3_ok"] = r["G3_p95"]["verdict"] != "worse" and r["G3_p50_verdict"] != "worse"
    reading = {}
    for cand in CANDIDATES:
        rs = rules[cand]
        harsh = [rs[f"{b}-harsh"] for b in ("udp", "gns")]
        guards = {"G1": all(rs[f"{b}-{p}"]["G1_ok"] for b in ("udp", "gns") for p in PROFILES),
                  "G2": all(rs[f"{b}-{p}"]["G2_ok"] for b in ("udp", "gns") for p in PROFILES),
                  "G3": all(rs[f"{b}-slow"]["G3_ok"] for b in ("udp", "gns"))}
        rs["guards"] = guards
        if any(h["O1_over_limit"]["verdict"] == "unassessable" for h in harsh):
            reading[cand] = "unassessable"  # never a favourable reading through O1 or O2 (EXP-044, Codex)
            continue
        better = any(h["O1_over_limit"]["verdict"] == "better" or h["O2_p95"]["verdict"] == "better" for h in harsh)
        worse = any(h[o]["verdict"] == "worse" for h in harsh for o in ("O1_over_limit", "O2_p95", "O2_p99"))
        reading[cand] = "improves_the_tail" if better and not worse and all(guards.values()) else "not_shown"
    v["rules"] = {**rules, "reading": reading}
    return v


if __name__ == "__main__":
    sys.exit(ab.main(sys.argv[1:], BATCHES, judge, "ki35-caps", __doc__))
