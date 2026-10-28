"""EXP-049 runner: what the DTLS cookie exchange costs the KI-35 tail. Measurement only: every batch runs today's
default configuration (arm off). Runs ki35_reconnect_probe in the predeclared batches, records everything immutably,
and applies the rules predeclared in docs/soak-testing.md ("Predeclared experiment KI-35: what the cookie exchange
costs the tail").
  python -I tools/ki35/run_cookie_probe.py build-gns/Release/ki35_reconnect_probe.exe
Writes evidence/logs/ki35-cookie-<time>-<commit7>[-dirty]/ (per-batch cycles and client logs, run.json, verdict.json);
never overwrites. --analyze DIR re-reads a recorded directory without running anything.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_ab_probe as ab  # noqa: E402  (the batch loop, batch names)
import run_reconnect_probe as rc  # noqa: E402  (Wilson, homogeneity, percentiles)

LIMIT_MS = rc.RECOVERY_LIMIT_MS  # 3000: the soak's recovery limit
THRESHOLD = 0.25                 # R_wait: "explain" if the 95% lower bound >= this, "do not explain" if the upper < it
MIN_S = 30                       # fewer reconnects over the limit: not assessable
UNATTRIBUTABLE_MAX = 0.10        # above: the backend is not assessable
# (backend, seed, arm, profile, cycles); GNS loss cannot be seeded, so its "seed" is a repetition id.
BATCHES = ([(b, s, "off", "harsh", 100) for b, seeds in (("udp", (4901, 4902, 4903, 4904)), ("gns", (1, 2, 3, 4)))
            for s in seeds]
           + [(b, s, "off", "slow", 100) for b, seeds in (("udp", (4905, 4906)), ("gns", (5, 6))) for s in seeds])
FIELDS = ("notice", "first_hello", "connected", "attempts", "attempt_hello", "last_hello0", "attempt_cookie_hello",
          "attempt_auth", "hello0_tx", "host_hello0_rx", "host_hvr_tx")
PHASES = ("notice", "establish", "cookie", "handshake", "session")
EXP047 = rc.ROOT / "evidence" / "logs" / "ki35-caps-2026-10-28-005304-7fcb88c"  # the secondary replication's data


def reconnected(c: dict) -> bool:
    """Counted reconnects: timeouts, failures and reconnects that never started are counted apart and excluded."""
    return c["outcome"] in ("ok", "no_snapshot") and c["connected"] >= 0


def attributable(c: dict) -> bool:
    """A final attempt that authenticated, every field present, and the predeclared orderings."""
    if any(c.get(f) is None for f in FIELDS) or c["attempts"] < 1 or c["hello0_tx"] < 1:
        return False
    chain = (c["first_hello"], c["attempt_hello"], c["last_hello0"], c["attempt_cookie_hello"], c["attempt_auth"],
             c["connected"])
    return min(chain) >= 0 and all(a <= b for a, b in zip(chain, chain[1:]))


def phases(c: dict) -> dict:
    """The five phases of an attributable reconnect; they add up to `total` (= connected)."""
    return {"notice": c["notice"], "establish": c["attempt_hello"] - c["notice"],
            "cookie": c["attempt_cookie_hello"] - c["attempt_hello"],
            "handshake": c["attempt_auth"] - c["attempt_cookie_hello"], "session": c["connected"] - c["attempt_auth"]}


def cookie_wait(c: dict) -> float:
    return c["last_hello0"] - c["attempt_hello"]


def flight(c: dict) -> str:
    if c["hello0_tx"] == 1:
        return "clean"
    return "hello_lost" if c["host_hello0_rx"] < c["hello0_tx"] else "hvr_lost_or_late"


def share(k: int, n: int) -> dict:
    return {"k": k, "n": n, "p": round(k / n, 4) if n else None, "ci95": rc.wilson(k, n)}


def rescued(slow: list[dict], cut) -> dict:
    """The share of `slow` (total > 3 s) whose total minus `cut(c)` is within the limit: an accounting ceiling."""
    return share(sum(c["connected"] - cut(c) <= LIMIT_MS for c in slow), len(slow))


def r_wait_verdict(k: int, n: int) -> str:
    if n < MIN_S:
        return "not_assessable"
    lo, hi = rc.wilson(k, n, exact=True)
    return "explain_part_of_the_tail" if lo >= THRESHOLD else "do_not_explain_the_tail" if hi < THRESHOLD else "inconclusive"


def describe(att: list[dict]) -> dict:
    """Reported, not rules: phases in and outside S, every ceiling, flights, restarts."""
    slow = [c for c in att if c["connected"] > LIMIT_MS]
    fast = [c for c in att if c["connected"] <= LIMIT_MS]
    ph = lambda cs: {p: {"p50": rc.pct([phases(c)[p] for c in cs], 50), "p95": rc.pct([phases(c)[p] for c in cs], 95),
                         "mean_share": (round(sum(phases(c)[p] / c["connected"] for c in cs if c["connected"] > 0)
                                              / len(cs), 4) if cs else None)} for p in PHASES}
    largest = {p: 0 for p in PHASES}
    for c in slow:
        q = phases(c)
        largest[max(PHASES, key=lambda p: q[p])] += 1
    no_restart = [c for c in slow if c["attempts"] == 1]
    return {"n": len(att), "over_limit": share(len(slow), len(att)),
            "R_wait": rescued(slow, cookie_wait), "R_cookie": rescued(slow, lambda c: phases(c)["cookie"]),
            "R_phase": {p: rescued(slow, lambda c, p=p: phases(c)[p]) for p in PHASES if p != "cookie"},
            "R_wait_no_restart": rescued(no_restart, cookie_wait),
            "restarted": sum(c["attempts"] > 1 for c in att),
            "phases_in_S": ph(slow), "phases_outside_S": ph(fast), "largest_phase_in_S": largest,
            "flights": {f: sum(flight(c) == f for c in att) for f in ("clean", "hello_lost", "hvr_lost_or_late")},
            "hello0_resent": share(sum(c["hello0_tx"] >= 2 for c in att), len(att)),
            "hello0_tx": {"mean": round(sum(c["hello0_tx"] for c in att) / len(att), 3) if att else None,
                          "max": max((c["hello0_tx"] for c in att), default=None)},
            "hvr_tx_mean": round(sum(c["host_hvr_tx"] for c in att) / len(att), 3) if att else None,
            "cookie_wait": {"p50": rc.pct([cookie_wait(c) for c in att], 50),
                            "p95": rc.pct([cookie_wait(c) for c in att], 95),
                            "p99": rc.pct([cookie_wait(c) for c in att], 99)}}


def summarize(cycles: list[dict]) -> dict:
    rec = [c for c in cycles if reconnected(c)]
    att = [c for c in rec if attributable(c)]
    return {"cycles": len(cycles), "events": {e: sum(c["outcome"] == e for c in cycles)
                                              for e in ("timeout", "failed", "no_reconnect", "no_snapshot")},
            "reconnected": len(rec), "unattributable": len(rec) - len(att), **describe(att)}


def completeness(out: Path, raw: dict[str, list[dict]]) -> list[str]:
    run_path = out / "run.json"
    if not run_path.exists():
        return ["no run.json"]
    run = json.loads(run_path.read_text(encoding="utf-8"))
    want = {ab.name(b, s, a, p): (a, p, n) for b, s, a, p, n in BATCHES}
    problems = [] if sorted(raw) == sorted(want) else [f"batches {sorted(raw)} != {sorted(want)}"]
    for nm, cs in raw.items():
        if nm not in want:
            continue
        a, p, n = want[nm]
        if [c["cycle"] for c in cs] != list(range(n)):
            problems.append(f"{nm}: cycles not 0..{n - 1} ({len(cs)} recorded)")
        if any(c.get("arm") != a or c.get("profile") != p for c in cs):
            problems.append(f"{nm}: arm/profile not recorded as {a}/{p}")
        # A missing attempt field makes that reconnect unattributable (the 10% rule), not the run incomplete (Codex).
        if any(f not in c for c in cs for f in ("outcome", "connected")):
            problems.append(f"{nm}: a cycle without its outcome")
        if not (out / nm / "client.log").exists():
            problems.append(f"{nm}: no client log")
        if run.get("exit_codes", {}).get(nm) != 0:
            problems.append(f"{nm}: exit {run.get('exit_codes', {}).get(nm)}")
    return problems


def replication(d: Path = EXP047) -> dict | None:
    """Secondary, reported only: EXP-047's off harsh batches have no attempt split, so `cookie` runs from first_hello."""
    if not d.is_dir():
        return None
    out = {}
    for backend in ("udp", "gns"):
        cs = [c for k in sorted(p for p in d.iterdir() if p.is_dir() and p.name.startswith(backend + "-")
                                and p.name.endswith("-off-harsh"))
              for c in rc.load_cycles(k) if reconnected(c) and 0 <= c["first_hello"] <= c["cookie_hello"]]
        slow = [c for c in cs if c["connected"] > LIMIT_MS]
        ck = [c["cookie_hello"] - c["first_hello"] for c in cs]
        out[backend] = {"n": len(cs), "over_limit": len(slow),
                        "R_cookie": rescued(slow, lambda c: c["cookie_hello"] - c["first_hello"]),
                        "cookie": {"p50": rc.pct(ck, 50), "p95": rc.pct(ck, 95), "p99": rc.pct(ck, 99)}}
    return out


def judge(out: Path) -> dict:
    raw = {d.name: rc.load_cycles(d) for d in sorted(p for p in out.iterdir() if p.is_dir())}
    problems = completeness(out, raw)
    v = {"complete": not problems, "incomplete_because": problems, "batches": {}, "pooled": {}, "rules": {}}
    if problems:  # fields may be missing: nothing is summarised, so nothing can be read as a result
        v["rules"]["reading"] = {"verdict": "incomplete", "next": None}
        return v
    v["batches"] = {k: summarize(cs) for k, cs in raw.items()}
    rules = {}
    for backend in ("udp", "gns"):
        for profile in ("harsh", "slow"):
            keys = [k for k in raw if k.startswith(backend + "-") and k.endswith(f"-{profile}")]
            v["pooled"][f"{backend}-{profile}"] = summarize([c for k in keys for c in raw[k]])
        keys = [k for k in raw if k.startswith(backend + "-") and k.endswith("-harsh")]
        po, r = v["pooled"][f"{backend}-harsh"], {}
        att = {k: [c for c in raw[k] if reconnected(c) and attributable(c)] for k in keys}
        hom = {}
        for nm, test in (("over_limit", lambda c: c["connected"] > LIMIT_MS), ("hello0_resent", lambda c: c["hello0_tx"] >= 2)):
            counts = [(sum(test(c) for c in cs), len(cs)) for cs in att.values()]
            hom[nm] = rc.homogeneity_p(counts, exact=True) if len(counts) == 4 and all(n for _, n in counts) else None
        r["homogeneity_p"] = {k: x if x is None else round(x, 6) for k, x in hom.items()}
        r["unattributable"] = share(po["unattributable"], po["reconnected"])
        k, n = po["R_wait"]["k"], po["R_wait"]["n"]
        if not po["reconnected"] or po["unattributable"] > UNATTRIBUTABLE_MAX * po["reconnected"]:
            r["R_wait_verdict"] = "not_assessable"
        elif None in hom.values():
            r["R_wait_verdict"] = "unassessable_homogeneity"
        elif any(x < rc.HOMOGENEITY_P for x in hom.values()):
            r["R_wait_verdict"] = "heterogeneous"  # per batch only: v["batches"]
        else:
            r["R_wait_verdict"] = r_wait_verdict(k, n)
        r["R_wait_ci95_exact"] = rc.wilson(k, n, exact=True)
        rules[backend] = r
    nxt = {"explain_part_of_the_tail": "predeclare_a_behavioural_cookie_experiment",
           "do_not_explain_the_tail": "return_to_gns_establishment"}.get(rules["gns"]["R_wait_verdict"],
                                                                        "owner_and_reviewer_choose")
    v["rules"] = {**rules, "reading": {"verdict": rules["gns"]["R_wait_verdict"], "next": nxt}}
    v["secondary_exp047"] = replication()
    return v


if __name__ == "__main__":
    sys.exit(ab.main(sys.argv[1:], BATCHES, judge, "ki35-cookie", __doc__))
