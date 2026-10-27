"""One M1 tier A session on the real game (EXP-048, docs/m1-criteria.md section 8). Run by the owner, elevated.

The tool never writes to the game, its install, its saves or its process: it checks the pinned build, snapshots the
predeclared folders (hashes; the install by size and time only), traces the network around the whole game run,
guides the scripted actions with beeps, then reads the permitted text files read-only looking for the player's
position, and classifies the session with session.classify. Saves are hashed, never read.

The owner launches and closes the game by hand. Before it starts: saves copied aside offline, GOG Galaxy and Steam
closed, the game not running. Writes evidence/logs/m1-tier-a-<time>-s<N>/session.json (no local paths, no machine or
user names, no save ids) and never overwrites one.
  python -I tools/m1/tier_a.py --session N [--repo <project root>]
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "environment"))
import check_gog_build as gog  # noqa: E402
import dryrun  # noqa: E402
import netwatch  # noqa: E402
import session  # noqa: E402

GAME = "ShadowOfWar.exe"
MUST_BE_CLOSED = (GAME, "GalaxyClient.exe", "steam.exe")
# The predeclaration (EXP-048). Labels: user = %LOCALAPPDATA%/WB Games/Shadow of War, install = GOG's install folder.
GAME_WRITTEN = ["user/*_ShadowOfWar.sav", "user/render.cfg", "user/gpu.nfo"]
ACTIONS = [{"t0": 0, "t1": 30, "kind": "still"}, {"t0": 30, "t1": 60, "kind": "move"}]
TOLERANCES = {"still_max": 0.5, "move_min": 5.0, "min_obs": 3}
GRACE_S = 10  # from the owner's Enter to t=0: time to switch back to the game
TEXT = {".cfg", ".ini", ".log", ".txt", ".json", ".xml", ".nfo"}  # permitted reads; saves (.sav) never
TEXT_MAX = 4 << 20
NUM = r"([-+]?(?:\d+(?:\.\d*)?|\.\d+))(?=[\s,;]|$)"  # a number must end cleanly: "1e6" or "1.#INF" is never 1
LABEL = r"(?i)\b(?:pos|position|loc|location)\b[^0-9+.-]{0,12}"  # never eats a sign or a decimal point
POS_LINE = re.compile(LABEL + NUM + r"[\s,;]+" + NUM + r"[\s,;]+" + NUM)
CANDIDATE = re.compile(LABEL + r"[-+]?(?:\.?\d|nan|inf)")  # one POS_LINE cannot read is an invalid observation
CLOCK = re.compile(r"\b([01]\d|2[0-3]):([0-5]\d):([0-5]\d(?:\.\d+)?)\b")


def running(names) -> list[str]:
    out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True).stdout.lower()
    return [n for n in names if f'"{n.lower()}"' in out]


def scan(roots: dict[str, Path]) -> tuple[list[tuple[str, str]], list[str], list[str]]:
    """Read the permitted text files read-only. Returns the candidate lines (source, line), the files read, and the
    permitted files that could not be read (too large or refused): those make the session INCONCLUSIVE."""
    found, scanned, unread = [], [], []
    for label, root in roots.items():
        paths, errors = session.walk(root)
        unread += [f"{label}/{d} (folder not listable)" for d in errors]
        for p in paths:
            if p.suffix.lower() not in TEXT or "x64" in p.relative_to(root).parts[:1]:
                continue
            rel = f"{label}/{p.name if p == root else p.relative_to(root).as_posix()}"
            kind = session.regular(p)
            if kind is False:  # a confirmed non-file
                continue
            if kind is None:
                unread.append(f"{rel} (a link, or not checkable: not read)")
                continue
            try:
                target = p.resolve()
                if p.is_symlink() or p.stat().st_nlink > 1 or not target.is_relative_to(root.resolve()) \
                        or target.suffix.lower() not in TEXT:
                    unread.append(f"{rel} (a link: not followed)")  # a link, hard or soft, could lead to a save
                    continue
                if p.stat().st_size > TEXT_MAX:
                    unread.append(f"{rel} (over {TEXT_MAX} bytes)")
                    continue
                with p.open("r", encoding="utf-8", errors="replace") as f:  # read-only
                    lines = f.readlines()
            except OSError as e:
                unread.append(f"{rel} ({type(e).__name__})")
                continue
            scanned.append(rel)
            found += [(rel, line.rstrip("\r\n")) for line in lines if CANDIDATE.search(line)]
    return found, scanned, unread


def observations(after: list[tuple[str, str]], before: list[tuple[str, str]], t0_wall: dt.datetime | None) -> list[dict]:
    """Only lines that were not there before the session count (a log kept from another day must not pass); a
    position is a labelled triple, its time the clock on the same line."""
    old = Counter(before)
    obs = []
    for src, line in after:
        if old[(src, line)]:
            old[(src, line)] -= 1
            continue
        m, c, t = POS_LINE.search(line), CLOCK.search(line), None
        if c and t0_wall:
            at = t0_wall.replace(hour=int(c.group(1)), minute=int(c.group(2)), second=0, microsecond=0)
            t = (at - t0_wall).total_seconds() + float(c.group(3))
            t += 86400 if t < -43200 else -86400 if t > 43200 else 0  # a clock line on the other side of midnight
        obs.append({"t": t, "pos": [float(v) for v in m.group(1, 2, 3)] if m else None, "source": src})
    return obs


MOVEMENT = ("position moved while standing still", "position did not follow the movement")  # session.classify's


def exit_code(target: dict) -> int | None:
    c = target.get("exit_code")
    return int(c) if isinstance(c, str) and c.strip().lstrip("-").isdigit() else None


def decide(rec: dict, games: list[dict], unread: list[str]) -> tuple[str, list[str]]:
    """session.classify, plus what only a real session has. An exit the trace did not record, or a menu answer the
    owner did not give, is missing evidence (INCONCLUSIVE), not a confirmed unclean exit; a recorded non-zero code or
    a "no" stays a FAIL. A permitted file that could not be read is unreadable evidence. Neither ever passes."""
    if rec["build_start"] != 0:
        return session.classify(rec)
    codes = [exit_code(g) for g in games if g["exited_in_trace"]]
    nonzero = [c for c in codes if c not in (0, None)]  # any instance's recorded non-zero exit is a FAIL
    code_seen = bool(nonzero) or (len(games) == 1 and codes == [0])
    menu = rec["exit"]["closed_by_menu"]
    missing = [] if code_seen else ["the game's exit was not observed in the trace"]
    missing += ["whether the game was closed through its menu was not recorded"] if menu is None and games else []
    observed = {"code": nonzero[0] if nonzero else 0, "closed_by_menu": True if menu is None else menu}
    outcome, reasons = session.classify(dict(rec, exit=observed))
    gaps = missing + [f"unreadable evidence: {u}" for u in unread]
    # A position that did not follow the actions is a FAIL only on otherwise sound evidence (section 6).
    if gaps and (outcome == "PASS" or (outcome == "FAIL" and all(r.startswith(MOVEMENT) for r in reasons))):
        outcome = "INCONCLUSIVE"
    return outcome, reasons + gaps


SAVE_ID = re.compile(r"[^/\\\s]+(?=_(?:\d+_)?ShadowOfWar\.sav)", re.I)


def redact(doc, paths: dict[str, Path], keep=("game_written",)):
    """No local paths, no machine or user names, no save ids (any save name prefix, from this machine or another).
    Works on the strings of the record, never on numbers, and leaves the predeclared patterns (`keep`) as they are."""
    def strings(o):
        if isinstance(o, dict):
            for k, v in o.items():
                if k not in keep:
                    yield from strings(v)
        elif isinstance(o, list):
            for v in o:
                yield from strings(v)
        elif isinstance(o, str):
            yield o
    names = {os.environ.get("COMPUTERNAME", ""), os.environ.get("USERNAME", "")} - {""}

    def plain(s: str) -> str:
        for token, p in paths.items():  # longest first, both slash styles
            for form in {str(p), p.as_posix()}:
                s = s.replace(form, token)
        for v in names:  # whole names only: a short user name must not eat into "closed" or "covered"
            s = re.sub(rf"(?<![A-Za-z0-9]){re.escape(v)}(?![A-Za-z0-9])", "<redacted>", s, flags=re.I)
        return s
    ids = {v: f"<id{i}>" for i, v in enumerate(sorted({m for s in strings(doc) for m in SAVE_ID.findall(plain(s))}
                                                     - {"<redacted>", "*"}), 1)}

    def fix(o):
        if isinstance(o, dict):
            return {k: (v if k in keep else fix(v)) for k, v in o.items()}  # keys are the record's schema
        if isinstance(o, list):
            return [fix(v) for v in o]
        if isinstance(o, str):
            return SAVE_ID.sub(lambda m: ids.get(m.group(0), m.group(0)), plain(o))
        return o
    return fix(doc)


def ask(prompt: str) -> bool | None:
    """y or n; anything else is asked again; no answer at all (end of input) is None, never a no."""
    while True:
        try:
            a = input(f"{prompt} [y/n] ").strip().lower()
        except EOFError:
            return None
        if a in ("y", "yes", "s", "sim"):
            return True
        if a in ("n", "no", "nao", "não"):
            return False


def beeps(n: int) -> None:
    try:
        import winsound
        for _ in range(n):
            winsound.Beep(880, 250)
            time.sleep(0.15)
    except (ImportError, RuntimeError):
        print("\a" * n, end="", flush=True)


def wait_until(t_mono: float) -> None:
    while (left := t_mono - time.monotonic()) > 0:
        time.sleep(min(left, 0.2))


def play(rec: dict) -> dt.datetime:
    """The owner's part, with the trace running. Returns the wall clock of t=0."""
    print(f"\nLaunch the game now ({GAME} directly, GOG Galaxy closed). Waiting for it to start...")
    while not running([GAME]):
        time.sleep(1)
    input("Load your save and stand somewhere open and safe. When you control the character, come back here and "
          "press Enter, then return to the game.")
    t0 = time.monotonic() + GRACE_S
    t0_wall = dt.datetime.now() + dt.timedelta(seconds=GRACE_S)
    print(f"t=0 in {GRACE_S} s. Then: ONE beep = stand still (0-30 s), TWO beeps = walk forward without stopping "
          "(30-60 s), THREE beeps = done.")
    wait_until(t0)
    beeps(1)
    wait_until(t0 + ACTIONS[1]["t0"])
    beeps(2)
    wait_until(t0 + ACTIONS[1]["t1"])
    beeps(3)
    print("Done. Now close the game through its menu (Quit to desktop). Waiting for it to exit...")
    while running([GAME]):
        time.sleep(1)
    rec["exit"]["closed_by_menu"] = ask("Did you close the game through its own menu?")  # None: not recorded
    for question, stop in (("Did the game show any integrity, tamper or debugger warning, or refuse to run?",
                            "protection_reaction"), ("Did the game crash or freeze?", "crash")):
        answer = ask(question)
        if answer is None:  # an unknown note: INCONCLUSIVE, never a silent no
            rec["operator_stops"].append(f"unanswered: {stop}")
        elif answer:
            rec["operator_stops"].append(stop)
    return t0_wall


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--session", type=int, required=True)
    ap.add_argument("--repo", type=Path, default=HERE.parents[1])
    a = ap.parse_args(argv)
    if not dryrun.elevated():
        print("Refused: run from an elevated PowerShell (the network trace needs it). Nothing was done.")
        return 2
    if busy := running(MUST_BE_CLOSED):
        print(f"Refused: close {', '.join(busy)} first. Nothing was done.")
        return 2
    if ask("Have you copied the saves aside, offline and outside the repository, for this session?") is not True:
        print("Refused: copy the saves first. Nothing was done.")
        return 2
    install, why = gog.registry_path()
    if install is None:
        print(f"Refused: GOG install not found ({why}). Nothing was done.")
        return 2
    user = Path(os.environ["LOCALAPPDATA"]) / "WB Games" / "Shadow of War"
    roots = {"user": user, "install": install}
    out_root = (a.repo / "evidence" / "logs").resolve()
    # We write in both: never inside the game's folders. TEMP is read from the environment, because
    # tempfile.gettempdir() itself writes a probe file into the folder it picks.
    temps = [Path(v).resolve() for v in (os.environ.get(k) for k in ("TMPDIR", "TEMP", "TMP")) if v]
    for ours in (out_root, *temps):
        if any(ours.is_relative_to(r.resolve()) or r.resolve().is_relative_to(ours) for r in roots.values()):
            print(f"Refused: {ours.name} overlaps the game's folders (check --repo and TEMP). Nothing was done.")
            return 2
    if not temps:
        print("Refused: TEMP is not set. Nothing was done.")
        return 2
    stamp = dt.datetime.now().strftime("%Y-%m-%d-%H%M%S")
    out_dir = out_root / f"m1-tier-a-{stamp}-s{a.session}"
    out_dir.mkdir(parents=True, exist_ok=False)  # evidence is immutable
    head, dirty = dryrun.git_state(a.repo)
    tmp = Path(tempfile.mkdtemp(prefix="mordor-m1-tier-a-", dir=temps[0]))  # the checked folder, never a fallback
    work = tmp / "net"
    rec = {"build_start": gog.verdict(gog.check(install))[1], "build_end": None,
           "before": session.snapshot(roots, {"install"}), "after": {}, "game_written": GAME_WRITTEN,
           "actions": ACTIONS, "tolerances": TOLERANCES, "operator_stops": [], "observations": [], "network": None,
           "exit": {"code": None, "closed_by_menu": None}}  # None: not recorded
    scanned, unread, t0_wall, games = [], [], None, []
    if rec["build_start"] == 0:
        lines_before, _, unread_before = scan(roots)
        started = False
        try:
            try:
                started = netwatch.start(work) == 0
                if started:
                    t0_wall = play(rec)
            except BaseException:  # Ctrl+C or an error: recorded; the trace is still stopped and analyzed
                rec["operator_stops"].append("interrupted")
        finally:  # whatever happened above, the system-wide trace is stopped and its raw files go (FB-35-04)
            if started:
                try:
                    netwatch.stop(work)
                except BaseException:
                    rec["operator_stops"].append("interrupted")
                    netwatch.abort(work)
                rec["network"] = netwatch.analyze(work, [GAME], [])  # deletes the raw trace on every path
            else:
                rec["network"] = {"raw_not_deleted": netwatch.abort(work), "verdict": "not_covered",
                                  "coverage_problems": ["the trace did not start; the game was not launched"]}
        if not started:
            print("The network trace did not start: do NOT launch the game. The session ends INCONCLUSIVE.")
        if "interrupted" in rec["operator_stops"]:
            print("Interrupted. If the game is still running, close it through its menu.")
        games = [t for t in rec["network"].get("targets", []) if t["image"].lower() == GAME.lower()]
        rec["exit"]["code"] = exit_code(games[0]) if len(games) == 1 else None
        rec["build_end"] = gog.verdict(gog.check(install))[1]
        rec["after"] = session.snapshot(roots, {"install"})
        lines_after, scanned, unread = scan(roots)
        unread = sorted(set(unread_before) | set(unread))  # a file unread before has no baseline: old lines look new
        rec["observations"] = observations(lines_after, lines_before, t0_wall)
    outcome, reasons = decide(rec, games, unread)
    net = rec["network"] or {}
    doc = {"experiment": "EXP-048", "session": a.session, "commit": head, "dirty": dirty, "outcome": outcome,
           "reasons": reasons, "build_start": rec["build_start"], "build_end": rec["build_end"],
           "changes": session.changes(rec["before"], rec["after"]) if rec["after"] else None,
           "files": {k: len(v) for k, v in rec["before"].items()}, "game_written": GAME_WRITTEN, "actions": ACTIONS,
           "tolerances": TOLERANCES, "scanned": scanned, "observations": rec["observations"], "exit": rec["exit"],
           "operator_stops": rec["operator_stops"],
           "network": {k: net.get(k) for k in ("verdict", "coverage", "coverage_problems", "canaries", "events_lost",
                                                "contacts", "dns", "targets", "raw_kept", "raw_not_deleted")}}
    paths = dict(sorted({"%TEMP%": tmp, "<install>": install, "%LOCALAPPDATA%": Path(os.environ["LOCALAPPDATA"])}.items(),
                        key=lambda kv: -len(str(kv[1]))))
    text = json.dumps(redact(doc, paths), indent=2, ensure_ascii=False) + "\n"
    (out_dir / "session.json").write_text(text, encoding="utf-8")
    shutil.rmtree(tmp, ignore_errors=True)  # only the trace state is left: analyze deleted the raw trace or failed
    print(f"\nSession {a.session}: {outcome}" + "".join(f"\n  - {r}" for r in reasons))
    print(f"Evidence: {out_dir.relative_to(a.repo).as_posix()}/session.json")
    return 0 if outcome == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
