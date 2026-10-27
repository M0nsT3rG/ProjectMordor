"""EXP-044 runner: DTLS retransmission back-off (arm off: today's 250 ms doubling to 1 s; arm flat: capped at 250 ms).
Runs ki35_reconnect_probe in the predeclared batches (arms alternating), records everything immutably, and applies the
rules predeclared in docs/soak-testing.md ("Predeclared experiment KI-35: DTLS retransmission back-off").
  python -I tools/ki35/run_timer_probe.py build-gns/Release/ki35_reconnect_probe.exe
Writes evidence/logs/ki35-timer-<time>-<commit7>[-dirty]/ (per-batch cycles and client logs, run.json, verdict.json);
never overwrites. --analyze DIR re-reads a recorded directory without running anything.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_ab_probe as ab  # noqa: E402  (Newcombe, bootstrap, censoring, the batch loop)
import run_reconnect_probe as rc  # noqa: E402  (loading, Wilson, homogeneity, percentiles)

ARMS = ("off", "flat")
COST_RATIO_MAX = 1.5  # C1: flat's mean handshake datagrams per reconnect / off's, per backend, harsh
# (backend, seed, arm, profile, cycles), arms alternating; GNS loss cannot be seeded, so its "seed" is a repetition id.
BATCHES = ([(b, s, a, "harsh", 100) for b, seeds in (("udp", (4401, 4402, 4403)), ("gns", (1, 2, 3))) for s in seeds for a in ARMS]
           + [(b, s, a, "latency", 50) for b, s in (("udp", 4404), ("gns", 4)) for a in ARMS])
TRACE_FIELDS = ("host_drops", "host_timers", "client_tx", "host_tx", "cpu_ms")


def direction(ci: list[float] | None) -> str:
    return ab.direction(ci).replace("on_", "flat_")


def mean(xs: list[float]) -> float | None:
    return round(sum(xs) / len(xs), 3) if xs else None


def summarize(cycles: list[dict]) -> dict:
    rec = [c for c in cycles if c["outcome"] != "no_reconnect"]
    totals = [ab.total_ms(c) for c in rec]
    over = sum(t > ab.LIMIT_MS for t in totals)
    dtls = [c["auth"] - c["first_hello"] for c in rec if c["auth"] >= 0 and c["first_hello"] >= 0]
    cpu = [c["cpu_delta"] for c in cycles if c.get("cpu_delta") is not None]  # set per batch by load()
    dgrams = [c["client_tx"] + c["host_tx"] for c in rec]
    return {"cycles": len(cycles), "reconnects": len(rec), "no_reconnect": len(cycles) - len(rec),
            "timeouts": sum(c["outcome"] == "timeout" for c in rec), "failed": sum(c["outcome"] == "failed" for c in rec),
            "no_snapshot": sum(c["outcome"] == "no_snapshot" for c in rec),  # reported; not a C2 event
            "total_ms": {f"p{q}": rc.pct(totals, q) for q in (50, 90, 95, 99)},
            "over_limit": {"k": over, "n": len(totals), "p": round(over / len(totals), 4) if totals else None,
                           "wilson95": rc.wilson(over, len(totals))},
            "dtls_ms": {f"p{q}": rc.pct(dtls, q) for q in (50, 95, 99)},
            "datagrams": {"sum": sum(dgrams), "n": len(dgrams)},  # C1 uses these, unrounded (Codex)
            "datagrams_per_reconnect": mean(dgrams),
            "timers_per_reconnect": mean([c["timers"] + c["host_timers"] for c in rec]),
            "hellos_per_reconnect": mean([c["hellos"] for c in rec]),
            "cpu_ms_per_cycle": mean(cpu)}


def traced(c: dict) -> bool:
    """The trace evidence a cycle's recorded phases imply (Codex passes 3 and 4): counts are non-negative integers;
    every ClientHello is a client datagram, and one is recorded iff the first one is; a HelloVerifyRequest received
    means the host sent; either side authenticated means both sides sent and a ClientHello left; Connected (no_snapshot
    included) means both sides authenticated."""
    if not all(isinstance(c[f], int) and c[f] >= 0 for f in ("client_tx", "host_tx", "host_timers", "timers", "hellos")):
        return False
    if c["client_tx"] < c["hellos"] or (c["hellos"] >= 1) != (c["first_hello"] >= 0):
        return False
    if c["hvr"] >= 0 and c["host_tx"] < 1:
        return False
    if (c["auth"] >= 0 or c["host_auth"] >= 0) and not (c["client_tx"] >= 1 and c["host_tx"] >= 1 and c["hellos"] >= 1):
        return False
    return c["connected"] < 0 or (c["auth"] >= 0 and c["host_auth"] >= 0)


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
        if any(f not in c for c in cs for f in TRACE_FIELDS):
            problems.append(f"{nm}: no host trace or cost fields")
        elif not all(traced(c) for c in cs):
            # Fields present but empty: a missing trace would read as a lower cost (Codex passes 2 and 3).
            problems.append(f"{nm}: a reconnect without the trace evidence its phases imply")
        if not (out / nm / "client.log").exists():
            problems.append(f"{nm}: no client log")
        if run.get("exit_codes", {}).get(nm) != 0:
            problems.append(f"{nm}: exit {run.get('exit_codes', {}).get(nm)}")
    return problems


C2_EVENTS = ("timeout", "failed", "no_reconnect")  # the predeclared ones, compared one by one (no offsetting)


def load(d: Path) -> list[dict]:
    cycles = rc.load_cycles(d)
    # Cumulative CPU per process: differences only within this batch (one process); cycle 0 includes the start.
    for a, b in zip(cycles, cycles[1:]):
        if a.get("cpu_ms", -1) >= 0 and b.get("cpu_ms", -1) >= 0:
            b["cpu_delta"] = b["cpu_ms"] - a["cpu_ms"]
    return cycles


def judge(out: Path) -> dict:
    raw = {d.name: load(d) for d in sorted(p for p in out.iterdir() if p.is_dir())}
    problems = completeness(out, raw)
    v = {"complete": not problems, "incomplete_because": problems,
         "batches": {k: summarize(cs) for k, cs in raw.items()} if not problems else {}, "pooled": {}, "rules": {}}
    if problems:  # fields may be missing: nothing is summarised, so nothing can be read as a result
        v["rules"]["reading"] = "incomplete"
        return v
    guards = {"C1_cost": True, "C2_robustness": True}
    for backend in ("udp", "gns"):
        for profile in ("harsh", "latency"):
            arms = {a: [c for k, cs in raw.items() if k.startswith(backend + "-") and k.endswith(f"-{a}-{profile}") for c in cs]
                    for a in ARMS}
            key = f"{backend}-{profile}"
            po = v["pooled"][key] = {a: summarize(cs) for a, cs in arms.items()}
            totals = {a: [ab.total_ms(c) for c in arms[a] if c["outcome"] != "no_reconnect"] for a in ARMS}
            flat_t, off_t = totals["flat"], totals["off"]
            dtls = {a: [c["auth"] - c["first_hello"] for c in arms[a]
                        if c["outcome"] != "no_reconnect" and c["auth"] >= 0 and c["first_hello"] >= 0] for a in ARMS}
            r = {"O3_p50_total": ab.boot_diff(flat_t, off_t, 50), "O3_p50_dtls": ab.boot_diff(dtls["flat"], dtls["off"], 50)}
            if profile == "harsh":
                o1 = ab.newcombe(po["flat"]["over_limit"]["k"], po["flat"]["over_limit"]["n"],
                                 po["off"]["over_limit"]["k"], po["off"]["over_limit"]["n"], exact=True)
                hom = {}
                for a in ARMS:
                    counts = [(v["batches"][k]["over_limit"]["k"], v["batches"][k]["over_limit"]["n"])
                              for k in raw if k.startswith(backend + "-") and k.endswith(f"-{a}-harsh")]
                    # All 3 batches must be assessable; a batch without reconnects makes it unknown (Codex).
                    hom[a] = rc.homogeneity_p(counts, exact=True) if len(counts) == 3 and all(n for _, n in counts) else None
                hetero = any(h is not None and h < rc.HOMOGENEITY_P for h in hom.values())  # exact (Codex pass 6)
                r["homogeneity_p"] = {a: h if h is None else round(h, 6) for a, h in hom.items()}
                # Classified on the exact bounds; only the recorded interval is rounded (Codex pass 5).
                r["O1_over_limit"] = {"diff_ci95": o1 and [round(x, 4) for x in o1],
                                      "verdict": "unassessable" if None in hom.values() else
                                      "heterogeneous" if hetero else direction(o1)}
                for q in (95, 99):
                    b = ab.boot_diff(flat_t, off_t, q)
                    r[f"O2_p{q}"] = {**b, "verdict": direction(b["ci95"])}
                d_off, d_flat = po["off"]["datagrams"], po["flat"]["datagrams"]
                ok_cost = d_off["sum"] > 0 and d_off["n"] > 0 and d_flat["n"] > 0
                # Unrounded: sum_flat / n_flat <= 1.5 * sum_off / n_off (Codex: a rounded 1.5004 passed).
                r["C1_datagram_ratio"] = round(d_flat["sum"] / d_flat["n"] / (d_off["sum"] / d_off["n"]), 4) if ok_cost else None
                guards["C1_cost"] &= ok_cost and d_flat["sum"] * d_off["n"] <= COST_RATIO_MAX * d_off["sum"] * d_flat["n"]
                c2 = {a: {e: sum(c["outcome"] == e for c in arms[a]) for e in C2_EVENTS} for a in ARMS}
                r["C2_events"] = c2
                guards["C2_robustness"] &= all(c2["flat"][e] <= c2["off"][e] for e in C2_EVENTS)
            v["rules"][key] = r
    harsh = [v["rules"][f"{b}-harsh"] for b in ("udp", "gns")]
    if any(h["O1_over_limit"]["verdict"] == "unassessable" for h in harsh):
        v["rules"]["guards"] = guards
        v["rules"]["reading"] = "unassessable"  # never growth_matters through O1 or O2 (Codex)
        return v
    better = any(h["O1_over_limit"]["verdict"] == "flat_better" or h["O2_p95"]["verdict"] == "flat_better" for h in harsh)
    worse = any(h[o]["verdict"] == "flat_worse" for h in harsh for o in ("O1_over_limit", "O2_p95", "O2_p99"))
    v["rules"]["guards"] = guards
    v["rules"]["reading"] = ("growth_matters" if better and not worse and all(guards.values())
                             else "timer_cap_not_the_lever")
    return v


if __name__ == "__main__":
    sys.exit(ab.main(sys.argv[1:], BATCHES, judge, "ki35-timer", __doc__))
