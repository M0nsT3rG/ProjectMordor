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


def ev(prov, eid, exec_pid, task="", **data):
    d = "".join(f'<Data Name="{k}">{v}</Data>' for k, v in data.items())
    info = f"<RenderingInfo Culture=\"en-US\"><Task>{task}</Task></RenderingInfo>" if task else ""
    return (f'<Event xmlns="{NS}"><System><Provider Name="{prov}" /><EventID>{eid}</EventID>'
            f'<Execution ProcessID="{exec_pid}" ThreadID="1" /></System><EventData>{d}</EventData>{info}</Event>')


def net(eid, pid, daddr="127.0.0.1", dport=9, udp=None):
    udp = eid in ("42", "43", "58", "59") if udp is None else udp  # as tracerpt renders the task on this machine
    return ev(nw.NET, eid, pid, task="KERNEL_NETWORK_TASK_UDPIP" if udp else "KERNEL_NETWORK_TASK_TCPIP",
              PID=f"   {pid}", size="13", daddr=daddr, saddr="127.0.0.1" if ":" not in daddr else "::1", dport=dport,
              sport="5")


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

    def test_raw_trace_lifecycle(self):  # FB-34-04
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            for n in nw.RAW:
                (work / n).write_bytes(b"old")
            self.assertEqual(nw.cleanup(work), [])
            self.assertEqual(sorted(p.name for p in work.iterdir()), [])
            (work / "trace.xml").mkdir()  # an undeletable leftover from an interrupted run
            self.assertEqual(nw.start(work), 2)  # never starts a new trace beside an old one
            state = json.loads((work / "state.json").read_text(encoding="utf-8"))
            self.assertEqual((state["trace_started"], state["stale_raw_left"]), (False, ["trace.xml"]))
        with tempfile.TemporaryDirectory() as tmp:  # Codex pass 2: an undeletable providers.txt
            (Path(tmp) / "providers.txt").mkdir()
            self.assertEqual(nw.start(Path(tmp)), 2)
        r = self.run_case(canary_events(100, "start") + GAME + canary_events(101, "end"))
        self.assertEqual((r["raw_kept"], r["raw_not_deleted"], r["contacts"][0]["ip"]), (False, [], "v4"))

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


class CoverageChecks(unittest.TestCase):  # EXP-046: a MATCH needs the claimed attribution (Codex, review #34)
    def c(self, proto, ip, peer="public", child=False, eid="12"):
        return {"event_id": eid, "proto": proto, "ip": ip, "peer": peer, "sport": "5", "dport": "9",
                "from_child": child, "count": 1}

    def test_no_false_confirmation(self):
        import coverage as cv
        sent = {"loop6_tcp": "sent", "loop6_udp": "sent"}
        two_tcp = {"verdict": "clean", "contacts": [self.c("tcp", "v6", "loopback", eid="28"),
                                                     self.c("tcp", "v6", "loopback", eid="29")], "dns": []}
        self.assertEqual(cv.covered_clean_with_v6(two_tcp, sent)[0], "MISMATCH")  # no IPv6 UDP attributed
        dns_only = {"verdict": "unexpected_contact", "contacts": [], "dns": [{"name": "x", "count": 1}]}
        for check in (cv.contact("tcp", "v4"), cv.contact("udp", "v6"), cv.child):
            self.assertEqual(check(dns_only, {})[0], "MISMATCH")
        parent_only = {"verdict": "unexpected_contact", "coverage": "covered", "contacts": [self.c("udp", "v4")],
                       "dns": [], "targets": [{}, {}]}
        self.assertEqual(cv.child(parent_only, {"child": 0})[0], "MISMATCH")  # the child's own send is required
        self.assertEqual(cv.child(dict(parent_only, contacts=[self.c("udp", "v4", child=True)]), {"child": 0})[0],
                         "MATCH")
        self.assertEqual(cv.child(parent_only, {"child udp 192.0.2.1": "refused by the OS (10051)"})[0],
                         "NOT_APPLICABLE")
        tcp = {"verdict": "unexpected_contact", "coverage": "covered", "contacts": [self.c("tcp", "v4")], "dns": []}
        attempt = cv.contact("tcp", "v4", never_connects=True)
        self.assertEqual(attempt(tcp, {"tcp 192.0.2.1": "attempted, never connected"})[0], "MATCH")
        self.assertEqual(attempt(tcp, {"tcp 192.0.2.1": "connected"})[0], "NOT_APPLICABLE")  # Codex pass 2
        self.assertEqual(attempt(tcp, {})[0], "MISMATCH")
        ambiguous = dict(tcp, coverage="not_covered")  # e.g. a reused PID: attribution is not proven
        self.assertEqual(attempt(ambiguous, {"tcp 192.0.2.1": "attempted, never connected"})[0], "MISMATCH")
        self.assertEqual(cv.loss({"verdict": "clean", "events_lost": 0}, {})[0], "NOT_DEMONSTRATED")

    def test_protocol_and_child_attribution_from_the_trace(self):
        body = canary_events(100, "start") + GAME + proc(777, "Reporter.exe", parent=500, parent_seq=5000) \
            + [net("42", 777, daddr="192.0.2.1"), net("28", 500, daddr="::1")] + canary_events(101, "end")
        with tempfile.TemporaryDirectory() as tmp:
            trace(Path(tmp), body)
            r = nw.analyze(Path(tmp), ["ShadowOfWar.exe"], [])
        got = {(c["proto"], c["ip"], c["peer"], c["from_child"]) for c in r["contacts"]}
        self.assertIn(("udp", "v4", "private", True), got)  # ipaddress counts TEST-NET-1 as not global ("private")
        self.assertIn(("tcp", "v6", "loopback", False), got)


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
        for raw in ({"raw_kept": True}, {"raw_not_deleted": ["trace.xml"]}):  # FB-34-04
            self.assertEqual(self.outcome(network={"verdict": "clean", "coverage_problems": [], **raw}), "FAIL", raw)
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


class TierA(unittest.TestCase):  # EXP-048: the real-session tool's pure parts
    def test_game_written_patterns(self):
        w = ["user/*_ShadowOfWar.sav", "user/render.cfg", "logs"]
        self.assertTrue(se.game_written("user/<id>_1_ShadowOfWar.sav", w))
        self.assertTrue(se.game_written("user/render.cfg", w))
        self.assertTrue(se.game_written("logs/a/b.log", w))  # a bare label covers everything under it
        self.assertFalse(se.game_written("user/CloudStorage/x_ShadowOfWar.sav", w))  # a pattern keeps its depth
        self.assertFalse(se.game_written("user/CloudStorage/featured-stm.1", w))
        self.assertFalse(se.game_written("install/game/new.log", w))
        s = good_session(game_written=["saves/*.sav", "logs"])
        s["after"] = json.loads(json.dumps(s["after"]))
        s["after"]["saves"]["new.sav"] = {"size": 1, "sha256": "X"}
        self.assertEqual(se.classify(s)[0], "PASS")
        s["after"]["saves"]["new.dat"] = {"size": 1, "sha256": "X"}
        self.assertEqual(se.classify(s)[0], "FAIL")

    def test_meta_only_and_single_file_roots(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "inst").mkdir()
            (Path(tmp) / "inst" / "a.dat").write_bytes(b"x" * 10)
            (Path(tmp) / "one.cfg").write_text("k=1\n", encoding="utf-8")
            snap = se.snapshot({"install": Path(tmp) / "inst", "one": Path(tmp) / "one.cfg"}, {"install"})
            self.assertEqual(set(snap["install"]["a.dat"]), {"size", "mtime_ns"})
            self.assertIn("sha256", snap["one"]["one.cfg"])

    def test_scan_and_redact(self):
        import datetime as dt
        import os
        import tier_a
        with tempfile.TemporaryDirectory() as tmp:
            user = Path(tmp) / "user"
            user.mkdir()
            (user / "render.cfg").write_text("WindowPosition 0 0 0\nWidth 1920\n", encoding="utf-8")
            (user / "x_ShadowOfWar.sav").write_text("position 1 2 3\n", encoding="utf-8")  # saves are never read
            log = user / "game.log"
            log.write_text("12:00:05 position 0 0 0\n", encoding="utf-8")  # kept from another day
            before = tier_a.scan({"user": user})[0]
            log.write_text("12:00:05 position 0 0 0\n10:00:05 player position: 1.5, -2, 3\nposition 4 5 6\n",
                           encoding="utf-8")
            (user / "big.log").write_bytes(b"x" * (tier_a.TEXT_MAX + 1))
            after, scanned, unread = tier_a.scan({"user": user})
            self.assertEqual(scanned, ["user/game.log", "user/render.cfg"])
            self.assertEqual(unread, [f"user/big.log (over {tier_a.TEXT_MAX} bytes)"])  # never silently skipped
            obs = tier_a.observations(after, before, dt.datetime(2026, 10, 27, 10, 0, 0))
            self.assertEqual(obs[0], {"t": 5.0, "pos": [1.5, -2.0, 3.0], "source": "user/game.log"})
            self.assertIsNone(obs[1]["t"])  # no clock on the line: classify calls it invalid, never PASS
            self.assertEqual(len(obs), 2)  # the old line does not count
            old = {k: os.environ.get(k) for k in ("COMPUTERNAME", "USERNAME")}
            os.environ.update(COMPUTERNAME="DESKTOP-TEST01", USERNAME="José")
            try:
                doc = {"a": str(user / "DESKTOP-TEST01_1_ShadowOfWar.sav"), "b": "4e57de68_ShadowOfWar.sav",
                       "c": "user/OLD-PC_2_ShadowOfWar.sav", "d": "user/José.log"}
                out = tier_a.redact(doc, {"%LOCALAPPDATA%": Path(tmp)})
                os.environ["USERNAME"] = "123"  # Codex pass 4: numbers and the predeclared patterns stay intact
                kept = tier_a.redact({"pos": [123.0, 1, 2], "game_written": ["user/*_ShadowOfWar.sav"]}, {})
            finally:
                for k, v in old.items():
                    os.environ.pop(k) if v is None else os.environ.update({k: v})
            text = json.dumps(out, ensure_ascii=False)
            for leak in (tmp, "DESKTOP-TEST01", "4e57de68", "OLD-PC", "José"):
                self.assertNotIn(leak, text)
            self.assertEqual(len(set(out.values())), 4)  # distinct ids stay distinct
            self.assertEqual(kept, {"pos": [123.0, 1, 2], "game_written": ["user/*_ShadowOfWar.sav"]})

    def test_unreadable_numbers_and_links_are_never_skipped(self):  # Codex pass 2
        import datetime as dt
        import tier_a
        with tempfile.TemporaryDirectory() as tmp:
            user = Path(tmp) / "user"
            user.mkdir()
            (user / "game.log").write_text("10:00:01 position 0 0 1e6\n", encoding="utf-8")
            after, _, unread = tier_a.scan({"user": user})
            obs = tier_a.observations(after, [], dt.datetime(2026, 10, 27, 10, 0, 0))
            self.assertEqual(obs, [{"t": 1.0, "pos": None, "source": "user/game.log"}])  # invalid, not [0, 0, 1]
            (user / "a_ShadowOfWar.sav").write_text("position 1 2 3\n", encoding="utf-8")
            try:
                (user / "backup.txt").symlink_to(user / "a_ShadowOfWar.sav")
            except OSError:
                self.skipTest("symlinks need a privilege on this machine")
            after, scanned, unread = tier_a.scan({"user": user})
            self.assertNotIn("user/backup.txt", scanned)
            self.assertIn("user/backup.txt (a link, or not checkable: not read)", unread)

    def test_decide_missing_exit_evidence_is_inconclusive_not_fail(self):  # Codex pass 2
        import tier_a
        rec = good_session(exit={"code": None, "closed_by_menu": True})
        partial = [{"image": "ShadowOfWar.exe", "started_in_trace": True, "exited_in_trace": False, "exit_code": None}]
        self.assertEqual(tier_a.decide(rec, partial, [])[0], "INCONCLUSIVE")
        self.assertEqual(tier_a.decide(rec, [], [])[0], "INCONCLUSIVE")  # never launched
        seen = [dict(partial[0], exited_in_trace=True, exit_code="0")]
        self.assertEqual(tier_a.decide(good_session(exit={"code": 0, "closed_by_menu": True}), seen, []), ("PASS", []))
        self.assertEqual(tier_a.decide(good_session(exit={"code": 0, "closed_by_menu": None}), seen, [])[0],
                         "INCONCLUSIVE")  # the owner's answer is missing
        self.assertEqual(tier_a.decide(good_session(exit={"code": 0, "closed_by_menu": False}), seen, [])[0], "FAIL")
        self.assertEqual(tier_a.decide(good_session(exit={"code": 3, "closed_by_menu": True}),
                                       [dict(seen[0], exit_code="3")], [])[0], "FAIL")  # the code comes from the trace
        self.assertEqual(tier_a.decide(good_session(exit={"code": 0, "closed_by_menu": True}), seen, ["user/x.log"])[0],
                         "INCONCLUSIVE")
        two = [dict(seen[0], exit_code="3"), seen[0]]  # Codex pass 3: one of two instances exited non-zero
        self.assertEqual(tier_a.decide(good_session(exit={"code": None, "closed_by_menu": True}), two, [])[0], "FAIL")

    def test_codex_pass_3(self):
        import datetime as dt
        import os
        import subprocess
        import tier_a
        line = [("user/game.log", "10:00:01 position .5 2 3")]
        self.assertEqual(tier_a.observations(line, [], dt.datetime(2026, 10, 27, 10))[0]["pos"], [0.5, 2.0, 3.0])
        for bad in ("0 0 1.#INF", "0 0 1.#QNAN", "0 0 1e6", "0 0 3)", "nan nan nan", "-nan 0 0", "inf 0 0"):  # Codex 5, 6
            line = f"10:00:01 position {bad}"
            self.assertTrue(tier_a.CANDIDATE.search(line), bad)  # never dropped before validation
            obs = tier_a.observations([("user/game.log", line)], [], dt.datetime(2026, 10, 27, 10))
            self.assertIsNone(obs[0]["pos"], bad)
        with tempfile.TemporaryDirectory() as tmp:  # Codex pass 6: a failed lookup is not a confirmed non-file
            self.assertIsNone(se.regular(Path(tmp) / "vanished.nfo"))
            self.assertFalse(se.regular(Path(tmp)))
            (Path(tmp) / "f").write_bytes(b"1")
            self.assertTrue(se.regular(Path(tmp) / "f"))
        static = good_session()["observations"]  # Codex pass 5: a movement FAIL needs otherwise sound evidence
        static = static[:3] + [dict(o, pos=[0, 0, 0]) for o in static[3:]]
        seen = [{"image": "ShadowOfWar.exe", "started_in_trace": True, "exited_in_trace": True, "exit_code": "0"}]
        self.assertEqual(tier_a.decide(good_session(observations=static), seen, [])[0], "FAIL")
        self.assertEqual(tier_a.decide(good_session(observations=static), seen, ["user/gpu.nfo (PermissionError)"])[0],
                         "INCONCLUSIVE")
        old = os.environ.get("USERNAME")
        os.environ["USERNAME"] = "Ed"
        try:
            doc = {"exit": {"closed_by_menu": True}, "verdict": "unexpected_contact", "who": "C:/Users/Ed/x", "c": "covered"}
            self.assertEqual(tier_a.redact(doc, {}), dict(doc, who="C:/Users/<redacted>/x"))
        finally:
            os.environ.pop("USERNAME") if old is None else os.environ.update(USERNAME=old)
        late = [("user/game.log", "00:00:05 position 1 2 3")]  # Codex pass 4: across midnight
        self.assertEqual(tier_a.observations(late, [], dt.datetime(2026, 10, 27, 23, 59, 50))[0]["t"], 15.0)
        with tempfile.TemporaryDirectory() as tmp:
            user = Path(tmp) / "user"
            (user / "CloudStorage").mkdir(parents=True)
            (user / "CloudStorage" / "a").write_bytes(b"1")
            (user / "a_ShadowOfWar.sav").write_text("position 1 2 3\n", encoding="utf-8")
            os.link(user / "a_ShadowOfWar.sav", user / "backup.txt")  # a hard link to a save
            outside = Path(tmp) / "outside"
            outside.mkdir()
            (outside / "x.log").write_text("position 1 2 3\n", encoding="utf-8")
            j = subprocess.run(["cmd", "/c", "mklink", "/J", str(user / "linked"), str(outside)], capture_output=True)
            _, scanned, unread = tier_a.scan({"user": user})
            self.assertIn("user/backup.txt (a link: not followed)", unread)
            self.assertNotIn("user/backup.txt", scanned)
            if j.returncode == 0:  # a junction is never entered, and is recorded
                self.assertIn("user/linked (folder not listable)", unread)
                self.assertNotIn("user/linked/x.log", scanned)
                self.assertEqual(tier_a.scan({"user": user / "linked"})[2], ["user/. (folder not listable)"])  # a linked root
                self.assertIn("error", se.snapshot({"user": user / "linked"})["user"]["./__unlistable__"])
        before = {"user": {"CloudStorage/a": {"size": 1, "sha256": "A"}, "r.cfg": {"size": 1, "sha256": "R"}}}
        after = {"user": {"CloudStorage/__unlistable__": {"error": "unlistable folder"}, "r.cfg": {"size": 1, "sha256": "R"}}}
        self.assertEqual(se.changes(before, after), {"added": [], "removed": [], "changed": []})
        s = good_session(before=before, after=after, game_written=["user/r.cfg"])
        self.assertEqual(se.classify(s)[0], "INCONCLUSIVE")  # lost access is unreadable evidence, not a removal


if __name__ == "__main__":
    unittest.main()
