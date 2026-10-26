"""Self-checks for the EXP-041 decision arithmetic: python -I tools/ki35/test_run_reconnect_probe.py"""
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_reconnect_probe as r  # noqa: E402


def cycle(**over):
    c = {"cycle": 0, "log_ms": 1000.0, "outcome": "ok", "reason": "connect_accept", "notice": 100.0, "inner": 600.0,
         "first_hello": 600.0, "hvr": 800.0, "cookie_hello": 800.0, "auth": 1400.0, "connected": 1600.0,
         "first_snapshot": 1650.0, "hellos": 2, "timers": 0, "loop_ms": 1.5,
         "attribution": {"attributed": False, "why": "fixture"}}  # load_batch always sets it
    c.update(over)
    return c


class Statistics(unittest.TestCase):
    def test_wilson_90_and_95(self):
        self.assertEqual(r.wilson(50, 100), [0.4038, 0.5962])  # textbook 95% Wilson for 50/100
        lo, hi = r.wilson(50, 100, r.Z90)
        self.assertTrue(0.40 < lo < 0.42 and 0.58 < hi < 0.60)

    def test_tost_three_outcomes(self):
        model = [0.5, 0.5]
        self.assertEqual(r.tost(500, 1000, model), "equivalent")      # 90% CI ~[0.474, 0.526] inside [0.42, 0.58]
        self.assertEqual(r.tost(20, 100, model), "different")         # 95% CI ~[0.13, 0.29] misses 0.5
        self.assertEqual(r.tost(45, 100, model), "inconclusive")      # too wide to be equivalent, still covers 0.5

    def test_homogeneity_exact_df2(self):
        self.assertEqual(r.homogeneity_p([(50, 100), (50, 100), (50, 100)]), 1.0)
        # X2 for (30,100),(50,100),(70,100): p = 0.5; each cell (20^2/50) + (20^2/50) for the outer two -> X2 = 32
        self.assertAlmostEqual(r.homogeneity_p([(30, 100), (50, 100), (70, 100)]), round(math.exp(-16), 6))
        self.assertIsNone(r.homogeneity_p([(3, 10)]))

    def test_lag1(self):
        self.assertAlmostEqual(r.lag1([0, 1] * 20), -0.975)  # alternating: strongly negative
        self.assertIsNone(r.lag1([1, 1, 1, 1]))


class Attribution(unittest.TestCase):
    def test_the_request_inside_the_round_trip_window_is_attributed(self):
        c = cycle()  # auth at log 2400, connected at log 2600
        sent = [2400.0, 2900.0]
        self.assertEqual(r.attribute(c, sent, [2590.0]), {"attributed": True, "index": 0, "requests": 1})

    def test_an_accept_logged_after_the_update_start_is_still_found(self):
        c = cycle()  # connected at log 2600, but the accept was logged at 2603 (Codex)
        self.assertEqual(r.attribute(c, [2400.0], [2603.0]), {"attributed": True, "index": 0, "requests": 1})

    def test_the_second_request_when_the_first_was_lost(self):
        c = cycle(connected=2200.0)  # connected at log 3200
        self.assertEqual(r.attribute(c, [2400.0, 2900.0], [3100.0])["index"], 1)

    def test_no_candidate_or_ambiguity_is_unattributed(self):
        c = cycle()
        self.assertEqual(r.attribute(c, [2400.0], [2450.0])["why"], "no_candidate")  # 50 ms: faster than any round trip
        c2 = cycle(connected=1800.0)
        self.assertEqual(r.attribute(c2, [2400.0, 2450.0], [2620.0])["why"], "ambiguous")  # both 170/220 ms before


def write_run(root: Path, udp_timers=(0, 0, 0), cycles=100, exit_codes=None, drop_last=False):
    """A complete predeclared run: the 6 batches of 100 cycles, exit 0 (overrides make it incomplete)."""
    codes = {}
    for backend, seed in r.BATCHES:
        name = f"{backend}-{seed}"
        timers = udp_timers[[4101, 4102, 4103].index(seed)] if backend == "udp" else 0
        d = root / name
        d.mkdir()
        n = cycles - 1 if drop_last and name == "gns-3" else cycles
        lines = [json.dumps(cycle(cycle=j, timers=timers if j % 10 else 0)) for j in range(n)]
        (d / "cycles.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
        (d / "client.log").write_text("", encoding="utf-8")
        codes[name] = 0
    codes.update(exit_codes or {})
    (root / "run.json").write_text(json.dumps({"cycles_per_batch": cycles, "batches": r.BATCHES, "exit_codes": codes}),
                                   encoding="utf-8")


class Tail(unittest.TestCase):
    def test_a_50_50_split_names_both_phases(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp))
            d = Path(tmp) / "udp-4101"  # slowest 10% of udp (30 of 300): 15 DTLS-heavy and 15 establish-heavy
            lines = [json.dumps(cycle(cycle=j, connected=9000.0, auth=8800.0, first_hello=600.0 if j % 2 else 8000.0,
                                      notice=100.0, inner=600.0)) if j < 30 else json.dumps(cycle(cycle=j))
                     for j in range(100)]
            (d / "cycles.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
            v = r.judge(Path(tmp))
            self.assertEqual(v["rules"]["udp"]["tail_dominant_phases"], ["dtls", "establish"])


class Completeness(unittest.TestCase):
    def test_an_incomplete_run_gets_no_verdict(self):
        for kw in ({"drop_last": True}, {"exit_codes": {"udp-4102": 4}}, {"cycles": 40}):
            with tempfile.TemporaryDirectory() as tmp:
                write_run(Path(tmp), **kw)
                v = r.judge(Path(tmp))
                self.assertFalse(v["complete"], kw)
                self.assertEqual(v["rules"]["udp"]["dtls_no_timer"]["verdict"], "incomplete", kw)
                self.assertEqual(v["rules"]["udp"]["tail_dominant_phases"], "incomplete", kw)

    def test_only_a_cut_last_log_line_is_tolerated(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "client.log"
            log.write_text('{"t_ms":1,"evt":"a"}\n{"t_ms":2,"ev', encoding="utf-8")
            self.assertEqual(len(r.read_log(log)), 1)
            log.write_text('{"t_ms":1,"ev\n{"t_ms":2,"evt":"a"}\n', encoding="utf-8")
            with self.assertRaises(json.JSONDecodeError):
                r.read_log(log)

    def test_a_directory_without_run_json_is_incomplete(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp) / "udp-4101"
            d.mkdir()
            (d / "cycles.jsonl").write_text(json.dumps(cycle()) + "\n", encoding="utf-8")
            self.assertEqual(r.judge(Path(tmp))["incomplete_because"], ["no run.json"])


class Eligibility(unittest.TestCase):
    def test_a_reconnect_without_snapshot_keeps_its_total(self):
        s = r.summarize([cycle(outcome="no_snapshot", first_snapshot=-1)], "udp")
        self.assertEqual(s["latency_ms"]["total"]["n"], 1)
        self.assertEqual(s["over_recovery_limit"]["k"], 0)       # Connected at 1600 ms is within 3 s
        self.assertEqual(s["latency_ms"]["first_snapshot"]["n"], 0)

    def test_authenticated_failures_stay_in_the_unattributed_share(self):
        cs = [cycle() for _ in range(8)] + [cycle(outcome="timeout", connected=-1, first_snapshot=-1) for _ in range(2)]
        for c in cs:
            c["attribution"] = r.attribute(c, [2400.0], [2590.0])
        s = r.summarize(cs, "udp")
        self.assertEqual((s["unattributed"]["k"], s["unattributed"]["n"]), (2, 10))
        self.assertEqual(s["over_recovery_limit"]["k"], 2)
    def test_proportions_never_condition_on_overall_success(self):
        cs = [cycle(), cycle(outcome="timeout", connected=-1, first_snapshot=-1, timers=3, auth=-1)]
        cs[0]["attribution"] = {"attributed": True, "index": 0}
        o = r.outcomes(cs, "gns")
        self.assertEqual(o["dtls_no_timer"], [1, 0])   # the timed-out reconnect still counts for DTLS
        self.assertEqual(o["session_first"], [1])
        self.assertEqual(o["gns_first_cycle"], [1, 1])  # inner 500 ms after Reconnecting in both

    def test_judge_marks_heterogeneous_batches_and_tail(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), udp_timers=(0, 0, 3))
            v = r.judge(Path(tmp))
            self.assertTrue(v["complete"], v["incomplete_because"])
            self.assertEqual(v["rules"]["udp"]["dtls_no_timer"]["verdict"], "heterogeneous")
            self.assertEqual(v["rules"]["udp"]["session_first"]["verdict"], "not_assessable")  # no accepts logged
            self.assertEqual(v["rules"]["udp"]["tail_dominant_phases"], ["dtls"])


if __name__ == "__main__":
    unittest.main()
