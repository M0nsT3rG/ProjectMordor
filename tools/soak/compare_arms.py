"""Compare soak runs of two arms of a predeclared one-variable experiment (review #12 FB-12-01).

Groups gate-v6 run directories by (profile, transport, secure) and by the run.json field `reconnect_retry_ms`
('default' counts as 500), and applies the decision rule documented in docs/soak-testing.md before the runs:
B is adopted only if, on every profile, it has no more client2_recovery_time failures than A, a median recovery
at least 10% lower, and no other gate failure that A does not have. Read-only; prints a table and the verdict.
Usage: python -I tools/soak/compare_arms.py <run_dir> [<run_dir> ...]
"""
from __future__ import annotations

import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

A_MS, B_MS = 500, 250


def ms(x) -> str:
    return "none" if x is None or x == math.inf else str(round(x))


def load(d: Path) -> dict | None:
    if not (d / "summary.json").is_file():
        return None
    s = json.loads((d / "summary.json").read_text(encoding="utf-8"))
    run = s["run"]
    retry = run.get("reconnect_retry_ms", "default")
    retry = A_MS if retry == "default" else int(retry)
    failed = {g["gate"] for g in s["gates"] if not g["passed"]}
    c2 = s["clients"]["client2"]
    rec = c2.get("recovery_after_stall_ms")  # None: the stalled client never reconnected, the worst outcome
    return {"dir": d.name, "key": f"{run['profile']}-{run['transport']}{'-sec' if run.get('secure') else ''}",
            "retry": retry, "recovery": math.inf if rec is None else rec, "failed": failed,
            "delivery": s["distributions"]["delivery_ratio"].get("client2"), "rtt_p50": c2.get("rtt_p50_ms")}


def verdict(groups: dict) -> tuple[bool, list[str]]:
    reasons, adopt = [], True
    for key, arms in sorted(groups.items()):
        a, b = arms.get(A_MS, []), arms.get(B_MS, [])
        if not a or not b:
            return False, [f"{key}: missing an arm"]
        fa = sum("client2_recovery_time" in r["failed"] for r in a)
        fb = sum("client2_recovery_time" in r["failed"] for r in b)
        ma = statistics.median(r["recovery"] for r in a)
        mb = statistics.median(r["recovery"] for r in b)
        other_a = set().union(*(r["failed"] for r in a)) - {"client2_recovery_time"}
        other_b = set().union(*(r["failed"] for r in b)) - {"client2_recovery_time"}
        ok1, ok2, ok3 = fb <= fa, mb <= 0.9 * ma, not (other_b - other_a)
        reasons.append(f"{key}: failures A {fa} / B {fb} ({'ok' if ok1 else 'NO'}), median A {ms(ma)} / B {ms(mb)} ms "
                       f"({'ok' if ok2 else 'NO'}), new other failures in B {sorted(other_b - other_a)} ({'ok' if ok3 else 'NO'})")
        adopt &= ok1 and ok2 and ok3
    return adopt, reasons


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2
    groups: dict = defaultdict(lambda: defaultdict(list))
    for d in map(Path, argv):
        r = load(d)
        if r:
            groups[r["key"]][r["retry"]].append(r)
        else:
            print(f"skipped (no summary): {d.name}")
    for key, arms in sorted(groups.items()):
        for retry, rs in sorted(arms.items()):
            rec = sorted(r["recovery"] for r in rs)
            print(f"{key} retry {retry} ms: n={len(rs)} recovery median {ms(statistics.median(rec))} max {ms(rec[-1])} ms, "
                  f"over 3000 ms {sum(x > 3000 for x in rec)}, runs {[ms(x) for x in rec]}, "
                  f"delivery {[r['delivery'] for r in rs]}, rtt_p50 {[ms(r['rtt_p50']) for r in rs]}")
    adopt, reasons = verdict(groups)
    for line in reasons:
        print("  " + line)
    print("VERDICT: adopt B" if adopt else "VERDICT: keep A (B not adopted)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
