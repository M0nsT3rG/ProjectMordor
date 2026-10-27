"""Tier A session evidence and outcome rules for M1 (docs/m1-criteria.md §6; FB-32-04, FB-33-03).

`snapshot` records size and SHA-256 of every file under labelled roots, opening each file read-only (saves: metadata
and hashes only, never their contents). `classify` turns one session record into exactly one outcome:
PASS, FAIL, INCONCLUSIVE or NOT RUN, with the reasons. It is the only place those rules live.

A session record (all keys required; the M1 predeclaration fixes `game_written` and `tolerances` before any session):
  build_start, build_end   check_gog_build.py exit codes (build_end None if the session never got there)
  before, after            snapshot() of the same roots
  game_written             labels the game itself is expected to change (its saves, its logs); nothing else may change
  network                  netwatch.py analyze result
  observations             [{"t": s, "pos": [x, y, z], "source": "<label>/<file>" or None}]
  actions                  [{"t0": s, "t1": s, "kind": "move" | "still"}]
  tolerances               {"still_max": game units, "move_min": game units, "min_obs": per action}
  exit                     {"code": process exit code or None, "closed_by_menu": bool}
  operator_stops           what the operator saw: "protection_reaction", "needed_write", "crash", "method_not_read_only"
"""
from __future__ import annotations

import hashlib
import math
from pathlib import Path

OPERATOR_STOPS = {"protection_reaction", "needed_write", "crash", "method_not_read_only"}


def _number(v) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def snapshot(roots: dict[str, Path]) -> dict:
    snap = {}
    for label, root in roots.items():
        files = {}
        for p in sorted(root.rglob("*")) if root.is_dir() else []:
            rel = p.relative_to(root).as_posix()
            if not p.is_file():
                continue
            try:
                h = hashlib.sha256()
                with p.open("rb") as f:  # read-only
                    for chunk in iter(lambda: f.read(1 << 20), b""):
                        h.update(chunk)
                files[rel] = {"size": p.stat().st_size, "sha256": h.hexdigest().upper()}
            except OSError as e:
                files[rel] = {"error": type(e).__name__}
        snap[label] = files if root.is_dir() else {"__missing_root__": {"error": "missing"}}
    return snap


def changes(before: dict, after: dict) -> dict:
    out = {"added": [], "removed": [], "changed": []}
    for label in sorted(set(before) | set(after)):
        b, a = before.get(label, {}), after.get(label, {})
        out["removed"] += [f"{label}/{k}" for k in sorted(set(b) - set(a))]
        out["added"] += [f"{label}/{k}" for k in sorted(set(a) - set(b))]
        out["changed"] += [f"{label}/{k}" for k in sorted(set(a) & set(b))  # unreadable is not a change: INCONCLUSIVE
                           if a[k] != b[k] and "error" not in a[k] and "error" not in b[k]]
    return out


def classify(s: dict) -> tuple[str, list[str]]:
    if s["build_start"] != 0:
        return "NOT RUN", [f"start build check exit {s['build_start']}"]
    fail, unsure = [], []
    if s["build_end"] != 0:
        fail.append(f"end build check exit {s['build_end']} (exe or DLL changed, or unverifiable)")
    fail += [f"operator stop: {x}" for x in s["operator_stops"] if x in OPERATOR_STOPS]
    unsure += [f"unknown operator note: {x}" for x in s["operator_stops"] if x not in OPERATOR_STOPS]
    ch = changes(s["before"], s["after"])
    fail += [f"removed: {f}" for f in ch["removed"]]
    fail += [f"unexpected write: {f}" for f in ch["added"] + ch["changed"] if f.split("/", 1)[0] not in s["game_written"]]
    for snap in (s["before"], s["after"]):
        unsure += [f"unreadable evidence: {lab}/{f}" for lab, fs in snap.items() for f, m in fs.items() if "error" in m]
    net = s["network"] or {"verdict": "not_covered", "coverage_problems": ["no network record"]}
    if net["verdict"] == "unexpected_contact":
        fail.append("unexpected online contact")
    elif net["verdict"] != "clean":
        unsure += [f"network monitoring: {p}" for p in net.get("coverage_problems", [])] or ["network not covered"]
    if s["exit"].get("code") != 0 or not s["exit"].get("closed_by_menu"):
        fail.append(f"no clean exit (code {s['exit'].get('code')}, closed by menu: {s['exit'].get('closed_by_menu')})")
    if fail:
        return "FAIL", fail + unsure
    files = {f"{lab}/{f}" for lab, fs in s["after"].items() for f in fs}
    obs = s["observations"]
    if not obs:
        unsure.append("no data: no position observed (Tier A INCONCLUSIVE / NO DATA)")
    unsure += [f"ambiguous provenance at t={o.get('t')}" for o in obs if o.get("source") not in files]
    bad = [o for o in obs if not (isinstance(o.get("pos"), list) and len(o["pos"]) == 3 and all(_number(v) for v in o["pos"])
                                  and _number(o.get("t")))]
    unsure += [f"invalid observation at t={o.get('t')}" for o in bad]  # NaN never compares: it would pass any tolerance
    obs = [o for o in obs if o not in bad]
    if {a.get("kind") for a in s["actions"]} != {"still", "move"}:
        unsure.append("the actions must include standing still and walking")
    if not all(_number(a.get("t0")) and _number(a.get("t1")) and a["t0"] < a["t1"] for a in s["actions"]):
        return "INCONCLUSIVE", unsure + ["an action has no valid time interval"]
    tol = s["tolerances"]
    if not (all(_number(tol.get(k)) and tol[k] > 0 for k in ("still_max", "move_min"))
            and isinstance(tol.get("min_obs"), int) and not isinstance(tol["min_obs"], bool) and tol["min_obs"] >= 2):
        return "INCONCLUSIVE", unsure + [f"invalid tolerances {tol}"]
    wrong = []
    for act in s["actions"]:
        pts = [o["pos"] for o in sorted(obs, key=lambda o: o["t"]) if act["t0"] <= o["t"] <= act["t1"]]  # chronological
        if len(pts) < tol["min_obs"]:
            unsure.append(f"too few observations in {act['kind']} {act['t0']}-{act['t1']} s: {len(pts)}")
            continue
        if act["kind"] == "still" and max(math.dist(pts[0], p) for p in pts) > tol["still_max"]:
            wrong.append(f"position moved while standing still ({act['t0']}-{act['t1']} s)")
        if act["kind"] == "move" and math.dist(pts[0], pts[-1]) < tol["move_min"]:
            wrong.append(f"position did not follow the movement ({act['t0']}-{act['t1']} s)")
    if unsure:
        return "INCONCLUSIVE", unsure + wrong
    if wrong:
        return "FAIL", wrong
    return "PASS", []
