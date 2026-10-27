"""Self-checks for the M1 tier A tools (FB-33-02/03): python -I tools/m1/test_m1.py

The trace fixtures copy the shape tracerpt produced on this machine (format probe, 2026-10-27); no real trace is used.
"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import netwatch as nw  # noqa: E402
import session as se  # noqa: E402

NS = "http://schemas.microsoft.com/win/2004/08/events/event"


def ev(prov, eid, exec_pid, **data):
    d = "".join(f'<Data Name="{k}">{v}</Data>' for k, v in data.items())
    return (f'<Event xmlns="{NS}"><System><Provider Name="{prov}" /><EventID>{eid}</EventID>'
            f'<Execution ProcessID="{exec_pid}" ThreadID="1" /></System><EventData>{d}</EventData></Event>')


def net(eid, pid, daddr="127.0.0.1", dport=9):
    return ev(nw.NET, eid, pid, PID=f"   {pid}", size="13", daddr=daddr, saddr="127.0.0.1", dport=dport, sport="5")


def canary_events(pid, tag):
    return [net("12", pid), net("42", pid), ev(nw.DNS, "3006", pid, QueryName=nw.canary_name(tag))]


def proc(pid, image, parent=1, start=True, stop=True, code=0, seq=None, parent_seq=1):
    seq = seq or pid * 10  # ProcessSequenceNumber, never reused (unlike the PID)
    out = []
    if start:
        out.append(ev(nw.PROC, "1", parent, ProcessID=f"   {pid}", ProcessSequenceNumber=seq,
                      ParentProcessID=f"   {parent}", ParentProcessSequenceNumber=parent_seq,
                      ImageName=f"\\Device\\HarddiskVolume1\\Games\\{image}"))
    if stop:
        out.append(ev(nw.PROC, "2", pid, ProcessID=f"   {pid}", ProcessSequenceNumber=seq, ExitCode=f"   {code}",
                      ImageName=image))
    return out


def trace(work: Path, body, lost=0, state=None):
    work.mkdir(parents=True, exist_ok=True)
    (work / "trace.xml").write_text("<Events>\n" + "\n".join(body) + "\n</Events>\n", encoding="utf-8")
    (work / "summary.txt").write_text(f"Total Events  Processed {len(body)}\nTotal Events  Lost      {lost}\n",
                                      encoding="utf-8")
    (work / "state.json").write_text(json.dumps(state or {"trace_started": True, "trace_stopped": True, "decoded": True,
                                                          "canary_start_pid": 100, "canary_end_pid": 101}),
                                     encoding="utf-8")


GAME = proc(500, "ShadowOfWar.exe") + [net("12", 500)]  # a loopback connect: allowed


class Netwatch(unittest.TestCase):
    def run_case(self, body, **kw):
        with tempfile.TemporaryDirectory() as tmp:
            trace(Path(tmp), body, **kw)
            r = nw.analyze(Path(tmp), ["shadowofwar.exe"], [])
            self.assertFalse((Path(tmp) / "trace.xml").exists())  # the raw trace is deleted
            return r

    def test_covered_and_clean(self):
        r = self.run_case(canary_events(100, "start") + GAME + canary_events(101, "end"))
        self.assertEqual((r["verdict"], r["coverage"]), ("clean", "covered"), r["coverage_problems"])
        self.assertNotIn("127.0.0.1", json.dumps(r))  # no addresses in the reduced result

    def test_any_non_loopback_or_lookup_is_unexpected(self):
        for extra in (net("42", 500, daddr="192.0.2.1"), ev(nw.DNS, "3006", 500, QueryName="example.com"),
                      net("12", 777, daddr="203.0.113.5")):  # 777: a child process of the game
            body = canary_events(100, "start") + GAME + proc(777, "Reporter.exe", parent=500, parent_seq=5000) + [extra] \
                + canary_events(101, "end")
            self.assertEqual(self.run_case(body)["verdict"], "unexpected_contact", extra)

    def test_other_processes_are_ignored(self):
        body = canary_events(100, "start") + GAME + proc(900, "browser.exe") + [net("12", 900, daddr="198.51.100.7")] \
            + canary_events(101, "end")
        self.assertEqual(self.run_case(body)["verdict"], "clean")

    def test_lost_coverage_never_passes(self):
        full = canary_events(100, "start") + GAME + canary_events(101, "end")
        cases = {"no end canary": canary_events(100, "start") + GAME,
                 "canary without DNS": canary_events(100, "start") + GAME + canary_events(101, "end")[:2],
                 "game started before the trace": canary_events(100, "start") + proc(500, "ShadowOfWar.exe", start=False)
                 + canary_events(101, "end"),
                 "no game at all": canary_events(100, "start") + canary_events(101, "end")}
        for why, body in cases.items():
            self.assertEqual(self.run_case(body)["verdict"], "not_covered", why)
        self.assertEqual(self.run_case(full, lost=3)["verdict"], "not_covered")
        self.assertEqual(self.run_case(full, state={"trace_started": False})["verdict"], "not_covered")
        with tempfile.TemporaryDirectory() as tmp:  # nothing decoded at all
            self.assertEqual(nw.analyze(Path(tmp), ["ShadowOfWar.exe"], [])["verdict"], "not_covered")
        reused = canary_events(100, "start") + proc(500, "old.exe", seq=1) + proc(500, "ShadowOfWar.exe", stop=False, seq=2) \
            + canary_events(101, "end")  # Codex: the old process's exit must not stand in for the game's
        self.assertEqual(self.run_case(reused)["verdict"], "not_covered")
        reused_late = canary_events(100, "start") + GAME + proc(500, "other.exe", seq=7) + canary_events(101, "end")
        self.assertEqual(self.run_case(reused_late)["verdict"], "not_covered")  # its traffic could be anyone's

    def test_codex_pass_2(self):
        full = canary_events(100, "start") + GAME + canary_events(101, "end")
        with tempfile.TemporaryDirectory() as tmp:  # a requested PID with no process events
            trace(Path(tmp), full)
            self.assertEqual(nw.analyze(Path(tmp), [], [500, 999])["verdict"], "not_covered")
        for state in ({"trace_started": True, "trace_stopped": False, "decoded": False, "canary_start_pid": 100,
                       "canary_end_pid": 101},):
            self.assertEqual(self.run_case(full, state=state)["verdict"], "not_covered")
        with tempfile.TemporaryDirectory() as tmp:  # truncated state, unreadable summary
            trace(Path(tmp), full)
            (Path(tmp) / "state.json").write_text("{", encoding="utf-8")
            self.assertEqual(nw.analyze(Path(tmp), ["ShadowOfWar.exe"], [])["verdict"], "not_covered")
        with tempfile.TemporaryDirectory() as tmp:
            trace(Path(tmp), full)
            (Path(tmp) / "summary.txt").unlink()
            (Path(tmp) / "summary.txt").mkdir()  # read_text raises an OSError
            self.assertEqual(nw.analyze(Path(tmp), ["ShadowOfWar.exe"], [])["verdict"], "not_covered")
        with tempfile.TemporaryDirectory() as tmp:  # Codex pass 3: every requested image must be seen
            trace(Path(tmp), full)
            self.assertEqual(nw.analyze(Path(tmp), ["ShadowOfWar.exe", "Missing.exe"], [])["verdict"], "not_covered")
        with tempfile.TemporaryDirectory() as tmp:  # Codex pass 3: stop() with a truncated state
            (Path(tmp) / "state.json").write_text("{", encoding="utf-8")
            self.assertEqual(nw.stop(Path(tmp), end_canary=False), 2)  # no session to stop: reported, no exception
        with tempfile.TemporaryDirectory() as tmp:  # one undeletable raw file does not keep the others
            trace(Path(tmp), full)
            (Path(tmp) / "trace.etl").mkdir()  # unlink() of a directory raises an OSError
            r = nw.analyze(Path(tmp), ["ShadowOfWar.exe"], [])
            self.assertEqual(r["raw_not_deleted"], ["trace.etl"])
            self.assertFalse((Path(tmp) / "trace.xml").exists())

    def test_a_seen_contact_fails_even_without_coverage(self):  # Codex: lost events never hide a recorded contact
        body = canary_events(100, "start") + GAME + [net("42", 500, daddr="192.0.2.1")] + canary_events(101, "end")
        self.assertEqual(self.run_case(body, lost=1)["verdict"], "unexpected_contact")

    def test_a_truncated_trace_is_not_covered_and_the_raw_files_are_deleted(self):
        with tempfile.TemporaryDirectory() as tmp:
            trace(Path(tmp), [])
            (Path(tmp) / "trace.xml").write_text("<Events>", encoding="utf-8")
            (Path(tmp) / "trace.etl").write_bytes(b"raw")
            self.assertEqual(nw.analyze(Path(tmp), ["ShadowOfWar.exe"], [])["verdict"], "not_covered")
            self.assertEqual([p.name for p in Path(tmp).iterdir()], ["state.json"])
            (Path(tmp) / "trace.etl").write_bytes(b"raw")  # decoding failed: an .etl but no .xml
            self.assertEqual(nw.analyze(Path(tmp), ["ShadowOfWar.exe"], [])["verdict"], "not_covered")
            self.assertFalse((Path(tmp) / "trace.etl").exists())


def good_session(**over):
    snap = {"saves": {"a.sav": {"size": 3, "sha256": "A"}}, "logs": {"game.log": {"size": 1, "sha256": "L"}},
            "config": {"render.cfg": {"size": 2, "sha256": "C"}}}
    after = json.loads(json.dumps(snap))
    after["logs"]["game.log"] = {"size": 9, "sha256": "L2"}  # the game appends its own log
    obs = [{"t": t, "pos": [0, 0, 0], "source": "logs/game.log"} for t in (1, 2, 3)] + \
          [{"t": t, "pos": [t * 2.0, 0, 0], "source": "logs/game.log"} for t in (11, 12, 13)]
    s = {"build_start": 0, "build_end": 0, "before": snap, "after": after, "game_written": ["saves", "logs"],
         "network": {"verdict": "clean", "coverage_problems": []}, "observations": obs,
         "actions": [{"t0": 0, "t1": 5, "kind": "still"}, {"t0": 10, "t1": 15, "kind": "move"}],
         "tolerances": {"still_max": 0.5, "move_min": 2.0, "min_obs": 3}, "exit": {"code": 0, "closed_by_menu": True},
         "operator_stops": []}
    s.update(over)
    return s


class Classify(unittest.TestCase):
    def outcome(self, **over):
        return se.classify(good_session(**over))[0]

    def test_pass(self):
        self.assertEqual(se.classify(good_session()), ("PASS", []))

    def test_not_run(self):
        self.assertEqual(self.outcome(build_start=3, build_end=None), "NOT RUN")
        self.assertEqual(self.outcome(build_start=5, build_end=None), "NOT RUN")

    def test_fail(self):
        s = good_session()
        after = json.loads(json.dumps(s["after"]))
        after["config"]["render.cfg"]["sha256"] = "X"
        self.assertEqual(self.outcome(after=after), "FAIL")  # a write outside the game-written places
        after = json.loads(json.dumps(s["after"]))
        del after["saves"]["a.sav"]
        self.assertEqual(self.outcome(after=after), "FAIL")  # a save removed
        self.assertEqual(self.outcome(build_end=3), "FAIL")  # exe or DLL changed
        self.assertEqual(self.outcome(network={"verdict": "unexpected_contact"}), "FAIL")
        self.assertEqual(self.outcome(exit={"code": 1, "closed_by_menu": True}), "FAIL")
        self.assertEqual(self.outcome(operator_stops=["crash"]), "FAIL")
        static = [dict(o, pos=[0, 0, 0]) for o in s["observations"]]
        self.assertEqual(self.outcome(observations=static), "FAIL")  # did not follow the movement

    def test_inconclusive(self):
        s = good_session()
        self.assertEqual(self.outcome(observations=[]), "INCONCLUSIVE")  # Tier A NO DATA
        vague = [dict(o, source=None) for o in s["observations"]]
        self.assertEqual(self.outcome(observations=vague), "INCONCLUSIVE")
        elsewhere = [dict(o, source="logs/other.log") for o in s["observations"]]
        self.assertEqual(self.outcome(observations=elsewhere), "INCONCLUSIVE")
        self.assertEqual(self.outcome(network={"verdict": "not_covered", "coverage_problems": ["x"]}), "INCONCLUSIVE")
        self.assertEqual(self.outcome(network=None), "INCONCLUSIVE")
        self.assertEqual(self.outcome(observations=s["observations"][:4]), "INCONCLUSIVE")  # too few in "move"
        after = json.loads(json.dumps(s["after"]))
        after["saves"]["a.sav"] = {"error": "PermissionError"}
        self.assertEqual(self.outcome(after=after), "INCONCLUSIVE")

    def test_codex_false_passes(self):
        nan = [dict(o, pos=[float("nan"), 0, 0]) for o in good_session()["observations"]]
        self.assertEqual(self.outcome(observations=nan), "INCONCLUSIVE")
        self.assertEqual(self.outcome(actions=[], observations=good_session()["observations"][:1]), "INCONCLUSIVE")
        self.assertEqual(self.outcome(actions=[{"t0": 0, "t1": 5, "kind": "still"}]), "INCONCLUSIVE")
        after = json.loads(json.dumps(good_session()["after"]))
        after["config"]["render.cfg"] = {"error": "PermissionError"}  # unreadable, not shown to have changed
        self.assertEqual(self.outcome(after=after), "INCONCLUSIVE")
        static = [dict(o, pos=[0, 0, 0]) for o in good_session()["observations"]]  # Codex pass 2
        for tol in ({"still_max": 0.5, "move_min": float("nan"), "min_obs": 3}, {"still_max": 0.5, "move_min": 0, "min_obs": 3},
                    {"still_max": 0.5, "move_min": 2.0, "min_obs": True}, {"still_max": 0.5, "move_min": 2.0}):
            self.assertEqual(self.outcome(observations=static, tolerances=tol), "INCONCLUSIVE", tol)
        typed = good_session()["observations"]
        typed[0] = dict(typed[0], pos=["bad", 0, 0])
        self.assertEqual(self.outcome(observations=typed), "INCONCLUSIVE")
        untimed = good_session()["observations"]  # Codex pass 4: an operator record without a timestamp
        untimed[0] = {"pos": [0, 0, 0], "source": None}
        self.assertEqual(self.outcome(observations=untimed), "INCONCLUSIVE")
        acts = [dict(a) for a in good_session()["actions"]]  # Codex pass 3
        acts[1]["t0"] = None
        self.assertEqual(self.outcome(actions=acts), "INCONCLUSIVE")
        shuffled = good_session()["observations"][:3] + [
            {"t": 11, "pos": [0, 0, 0], "source": "logs/game.log"}, {"t": 13, "pos": [0, 0, 0], "source": "logs/game.log"},
            {"t": 12, "pos": [3, 0, 0], "source": "logs/game.log"}]  # array order must not decide the endpoints
        self.assertEqual(self.outcome(observations=shuffled), "FAIL")

    def test_a_stop_condition_wins_over_inconclusive(self):
        self.assertEqual(self.outcome(observations=[], build_end=3), "FAIL")

    def test_snapshot_is_read_only_and_complete(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "saves"
            (root / "sub").mkdir(parents=True)
            (root / "sub" / "b.sav").write_bytes(b"opaque")
            before = (root / "sub" / "b.sav").stat().st_mtime_ns
            snap = se.snapshot({"saves": root, "gone": Path(tmp) / "absent"})
            self.assertEqual(set(snap["saves"]), {"sub/b.sav"})
            self.assertIn("error", snap["gone"]["__missing_root__"])
            self.assertEqual((root / "sub" / "b.sav").stat().st_mtime_ns, before)


if __name__ == "__main__":
    unittest.main()
