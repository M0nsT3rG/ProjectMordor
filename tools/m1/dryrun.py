"""Non-game dry run of the M1 tier A gates (FB-33-03, EXP-045). Never touches the game, its files or its saves.

Every case is one complete mini-session on fixtures in a temporary directory: a fixture install checked by
check_gog_build.check (with a fixture pin), labelled snapshots of fixture config, log and save folders, a real ETW
network trace around a stand-in process (tools/m1/standin.py), and session.classify. Each case injects one fault and
states its expected outcome before it runs; the result records expected against actual.

Network monitoring needs an elevated prompt. Run elevated, the baseline must PASS; run without elevation, every case
that depends on network coverage must come out INCONCLUSIVE (lost monitoring blocks) and the others unchanged.
Writes evidence/logs/m1-dryrun-<time>-<elevated|not-elevated>/result.json and never overwrites one.
  python -I tools/m1/dryrun.py [--repo <project root>]
"""
from __future__ import annotations

import argparse
import ctypes
import datetime as dt
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "environment"))
import netwatch  # noqa: E402
import session  # noqa: E402
import check_gog_build as gog  # noqa: E402

X64 = {"ShadowOfWar.exe": b"MZ fixture, not the game", "bink2w64.dll": b"fixture dll"}
FIXTURE_VERSION = lambda p: {"file_version_fixed": "1.0.0.0"}  # noqa: E731
ACTIONS = [{"t0": 0, "t1": 5, "kind": "still"}, {"t0": 10, "t1": 15, "kind": "move"}]
TOLERANCES = {"still_max": 0.5, "move_min": 2.0, "min_obs": 3}  # fixture units; the M1 predeclaration sets real ones
# name: (fault, expected when elevated, expected when not elevated)
CASES = {
    "baseline": (None, "PASS", "INCONCLUSIVE"),
    "start_build_changed": ("exe_before", "NOT RUN", "NOT RUN"),
    "dll_changed_mid_session": ("dll_during", "FAIL", "FAIL"),
    "unexpected_write": ("config_during", "FAIL", "FAIL"),
    "save_removed": ("save_removed", "FAIL", "FAIL"),
    "ambiguous_provenance": ("no_source", "INCONCLUSIVE", "INCONCLUSIVE"),
    "no_data": ("no_positions", "INCONCLUSIVE", "INCONCLUSIVE"),
    "position_does_not_follow": ("static", "FAIL", "INCONCLUSIVE"),
    "unexpected_online_contact": ("contact", "FAIL", "INCONCLUSIVE"),
    "lost_monitoring": ("no_end_canary", "INCONCLUSIVE", "INCONCLUSIVE"),
    "unclean_exit": ("exit_1", "FAIL", "FAIL"),
}
# The outcome must come for the right reason (Codex, review #34): a reason substring per case, (elevated, not elevated).
NO_TRACE = "the trace did not start"
REASONS = {
    "baseline": (None, NO_TRACE), "start_build_changed": ("start build check", "start build check"),
    "dll_changed_mid_session": ("end build check", "end build check"),
    "unexpected_write": ("unexpected write: config/", "unexpected write: config/"),
    "save_removed": ("removed: saves/", "removed: saves/"),
    "ambiguous_provenance": ("ambiguous provenance", "ambiguous provenance"), "no_data": ("no data", "no data"),
    "position_does_not_follow": ("did not follow", "did not follow"),
    "unexpected_online_contact": ("unexpected online contact", NO_TRACE),
    "lost_monitoring": ("end canary not fully seen", NO_TRACE), "unclean_exit": ("no clean exit", "no clean exit"),
}
POS = re.compile(r"^pos t=(\d+) (\S+) (\S+) (\S+)$")


def elevated() -> bool:
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except AttributeError:
        return False


def fixture(root: Path) -> tuple[Path, dict, dict]:
    inst = root / "install"
    (inst / "x64").mkdir(parents=True)
    for n, b in X64.items():
        (inst / "x64" / n).write_bytes(b)
    (inst / "goggame-1.info").write_text(json.dumps({"gameId": "1", "buildId": "2"}), encoding="utf-8")
    pin = {"game_id": "1", "build_id": "2", "file_version": "1.0.0.0", "addons": {},
           "x64": {n: hashlib.sha256(b).hexdigest().upper() for n, b in X64.items()}}
    roots = {"config": root / "config", "logs": root / "logs", "saves": root / "saves"}
    for r in roots.values():
        r.mkdir()
    (roots["config"] / "render.cfg").write_text("fixture=1\n", encoding="utf-8")
    (roots["logs"] / "game.log").write_text("fixture log\n", encoding="utf-8")
    for n in ("slot0.sav", "slot1.sav"):
        (roots["saves"] / n).write_bytes(b"\0opaque fixture save\0")
    return inst, pin, roots


def build(inst: Path, pin: dict) -> int:
    return gog.verdict(gog.check(inst, pin, FIXTURE_VERSION))[1]


def run_case(name: str, fault: str | None, tmp: Path) -> dict:
    inst, pin, roots = fixture(tmp / name)
    if fault == "exe_before":
        (inst / "x64" / "ShadowOfWar.exe").write_bytes(b"changed before the session")
    rec = {"build_start": build(inst, pin), "build_end": None, "before": session.snapshot(roots),
           "game_written": ["saves", "logs"], "actions": ACTIONS, "tolerances": TOLERANCES, "operator_stops": [],
           "observations": [], "network": None, "after": {}, "exit": {"code": None, "closed_by_menu": False}}
    if rec["build_start"] != 0:
        outcome, why = session.classify(rec)
        return {"outcome": outcome, "reasons": why, "network": None}
    work = tmp / name / "net"
    netwatch.start(work)
    args = [sys.executable, "-I", str(HERE / "standin.py"), "--log", str(roots["logs"] / "game.log"),
            "--mode", {"static": "static", "no_positions": "none"}.get(fault, "moving")]
    args += ["--contact"] if fault == "contact" else []
    args += ["--exit", "1"] if fault == "exit_1" else []
    p = subprocess.Popen(args)
    if fault == "dll_during":
        (inst / "x64" / "bink2w64.dll").write_bytes(b"changed during the session")
    if fault == "config_during":
        (roots["config"] / "render.cfg").write_text("fixture=2\n", encoding="utf-8")
    if fault == "save_removed":
        (roots["saves"] / "slot1.sav").unlink()
    code = p.wait(timeout=60)
    netwatch.stop(work, end_canary=fault != "no_end_canary")
    rec["network"] = netwatch.analyze(work, [], [p.pid])
    rec["build_end"] = build(inst, pin)
    rec["after"] = session.snapshot(roots)
    with (roots["logs"] / "game.log").open("r", encoding="utf-8") as f:  # read-only, like tier A
        for line in f:
            m = POS.match(line.strip())
            if m:
                rec["observations"].append({"t": int(m.group(1)), "pos": [float(v) for v in m.group(2, 3, 4)],
                                            "source": None if fault == "no_source" else "logs/game.log"})
    rec["exit"] = {"code": code, "closed_by_menu": True}  # a stand-in has no menu; the operator records it for the game
    outcome, why = session.classify(rec)
    net = rec["network"]
    return {"outcome": outcome, "reasons": why,
            "network": {k: net.get(k) for k in ("verdict", "coverage_problems", "canaries", "events_lost", "contacts",
                                                     "raw_not_deleted")}}


def git_state(repo: Path) -> tuple[str, bool]:
    head = subprocess.run(["git", "-c", "safe.directory=*", "rev-parse", "--short", "HEAD"], cwd=repo, capture_output=True, text=True).stdout.strip()
    dirty = bool(subprocess.run(["git", "-c", "safe.directory=*", "status", "--porcelain"], cwd=repo, capture_output=True, text=True).stdout.strip())
    return head, dirty


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", type=Path, default=HERE.parents[1])
    a = ap.parse_args(argv)
    up = elevated()
    stamp = dt.datetime.now().strftime("%Y-%m-%d-%H%M%S")
    out_dir = a.repo / "evidence" / "logs" / f"m1-dryrun-{stamp}-{'elevated' if up else 'not-elevated'}"
    out_dir.mkdir(parents=True, exist_ok=False)  # evidence is immutable
    head, dirty = git_state(a.repo)
    tmp = Path(tempfile.mkdtemp(prefix="mordor-m1-dryrun-"))
    results = []
    try:
        for name, (fault, exp_up, exp_down) in CASES.items():
            expected = exp_up if up else exp_down
            must = REASONS[name][0 if up else 1]
            r = run_case(name, fault, tmp)
            results.append({"case": name, "fault": fault, "expected": expected, "actual": r["outcome"],
                            "match": r["outcome"] == expected and (must is None or any(must in x for x in r["reasons"])),
                            "required_reason": must, "reasons": r["reasons"], "network": r["network"]})
            print(f"{name}: expected {expected}, actual {r['outcome']}{'' if results[-1]['match'] else '  <-- MISMATCH'}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    ok = all(x["match"] for x in results)
    doc = {"experiment": "EXP-045", "elevated": up, "commit": head, "dirty": dirty, "game_touched": False,
           "cases": results, "all_match": ok}
    text = json.dumps(doc, indent=2) + "\n"
    text = text.replace(str(tmp), "%TEMP%")  # no local paths in the evidence
    (out_dir / "result.json").write_text(text, encoding="utf-8")
    print(f"{'ALL CASES MATCH' if ok else 'SOME CASES DO NOT MATCH'}: {out_dir.relative_to(a.repo).as_posix()}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
