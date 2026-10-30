"""EXP-036 runner: runs ki35_probe, records the samples immutably, and applies the rules predeclared in
docs/soak-testing.md ("Predeclared experiment KI-35") before any run.
  python -I tools/ki35/run_probe.py build-gns/Release/ki35_probe.exe [harsh_trials] [clean_trials]
Writes evidence/logs/ki35-probe-<time>-<commit7>[-dirty]/{run.json, results.json, verdict.json}; never overwrites.
"""
from __future__ import annotations

import datetime
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bounded import run_bounded  # noqa: E402
# Fixed in the predeclaration (model.py output and the 39 recorded soak reconnects); never tuned to results.
MODEL = {"p50": 409.2, "p90": 1850.0, "p99": 3359.8}
SOAK_ESTABLISH_P50 = 880.0
H2_MARGIN_MS = 200.0


def pct(samples: list[float], p: int) -> float | None:
    s = sorted(samples)
    return round(s[min(len(s) - 1, int(p / 100 * len(s)))], 1) if s else None


def band(model: float) -> tuple[float, float]:
    w = max(0.25 * model, 150.0)
    return round(model - w, 1), round(model + w, 1)


def main(argv: list[str]) -> int:
    if not argv or not Path(argv[0]).is_file():
        print(__doc__)
        return 2
    git = lambda *a: subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    commit = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain", "--", ".", ":(exclude)evidence"))
    started = datetime.datetime.now()
    run_id = f"ki35-probe-{started:%Y-%m-%d-%H%M%S}-{commit[:7]}{'-dirty' if dirty else ''}"
    out = ROOT / "evidence" / "logs" / run_id
    out.mkdir(parents=True)  # raises if it exists: evidence is never overwritten
    code = run_bounded([str(Path(argv[0]).resolve()), *argv[1:]], out / "results.json", 3600)
    (out / "run.json").write_text(json.dumps({"run_id": run_id, "started": started.isoformat(timespec="seconds"),
                                              "commit": commit, "dirty": dirty, "probe_exit": code,
                                              "args": argv[1:]}, indent=2) + "\n", encoding="utf-8")
    if code != 0:  # hung or failed: the evidence stays as recorded, with no verdict
        print(f"probe exit {code}; recorded: {out.relative_to(ROOT).as_posix()}")
        return 1
    results = json.loads((out / "results.json").read_text(encoding="utf-8"))
    h, c = results["harsh"]["ms"], results["clean"]["ms"]
    measured = {"harsh": {k: pct(h, p) for k, p in (("p50", 50), ("p90", 90), ("p99", 99))},
                "clean": {k: pct(c, p) for k, p in (("p50", 50), ("p90", 90))},
                "harsh_n": len(h), "harsh_failed": results["harsh"]["failed"], "clean_n": len(c),
                "clean_failed": results["clean"]["failed"]}
    b50, b90 = band(MODEL["p50"]), band(MODEL["p90"])
    m50, m90 = measured["harsh"]["p50"], measured["harsh"]["p90"]
    h1 = m50 is not None and b50[0] <= m50 <= b50[1] and b90[0] <= m90 <= b90[1]
    h2 = m50 is not None and SOAK_ESTABLISH_P50 - m50 > H2_MARGIN_MS
    verdict = {"model": MODEL, "h1_bands": {"p50": b50, "p90": b90}, "measured": measured,
               "H1_gns_model_explains_raw_establishment": h1,
               "H2_our_path_adds_over_200ms": h2, "soak_establish_p50": SOAK_ESTABLISH_P50,
               "gap_ms": None if m50 is None else round(SOAK_ESTABLISH_P50 - m50, 1)}
    (out / "verdict.json").write_text(json.dumps(verdict, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(verdict, indent=2))
    print(f"recorded: {out.relative_to(ROOT).as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
