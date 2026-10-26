"""Self-checks for the EXP-043 decision arithmetic: python -I tools/ki35/test_run_ab_probe.py"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_ab_probe as r  # noqa: E402


def cycle(j, arm, profile, connected=1500.0, drops=(), gate=0, log_ms=None):
    base = j * 10_000.0 if log_ms is None else log_ms
    return {"cycle": j, "log_ms": base, "outcome": "ok", "reason": "connect_accept", "notice": 100.0, "inner": 600.0,
            "first_hello": 600.0, "hvr": 800.0, "cookie_hello": 800.0, "auth": 1400.0, "connected": connected,
            "first_snapshot": connected + 50, "hellos": 2, "timers": 0, "loop_ms": 1.5, "host_auth": 1300.0,
            "host_drops": list(drops), "gate_timeouts": gate, "arm": arm, "profile": profile}


def write_run(root: Path, slow_on: int = 10, slow_off: int = 30, defect: bool = False, drop_batch: str = ""):
    codes = {}
    for b, s, a, p, n in r.BATCHES:
        nm = r.name(b, s, a, p)
        if nm == drop_batch:
            continue
        d = root / nm
        d.mkdir()
        slow = (slow_on if a == "on" else slow_off) if p == "harsh" else 0
        lines, log = [], []
        for j in range(n):
            c = cycle(j, a, p, connected=4000.0 if j < slow else 1500.0,
                      drops=[1495.0] if (defect and a == "on" and j == 0) else [])
            lines.append(json.dumps(c))
            log.append(json.dumps({"t_ms": c["log_ms"] + 1400.0, "evt": "connect_request", "sent": True, "reconnecting": True}))
            log.append(json.dumps({"t_ms": c["log_ms"] + 1900.0, "evt": "connect_request", "sent": True, "reconnecting": True}))
            log.append(json.dumps({"t_ms": c["log_ms"] + c["connected"] - 10.0, "evt": "connect_accept_received"}))
        (d / "cycles.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
        (d / "client.log").write_text("\n".join(log) + "\n", encoding="utf-8")
        codes[nm] = 0
    (root / "run.json").write_text(json.dumps({"batches": r.BATCHES, "exit_codes": codes}), encoding="utf-8")


class Arithmetic(unittest.TestCase):
    def test_newcombe_matches_the_published_example(self):
        lo, hi = r.newcombe(56, 70, 48, 80)  # Newcombe (1998), example (a), method 10: 0.0524 to 0.3339
        self.assertAlmostEqual(lo, 0.0524, places=3)
        self.assertAlmostEqual(hi, 0.3339, places=3)

    def test_bootstrap_is_seeded_and_directional(self):
        on, off = [1000.0] * 50 + [2000.0] * 50, [3000.0] * 50 + [4000.0] * 50
        a, b = r.boot_diff(on, off, 95), r.boot_diff(on, off, 95)
        self.assertEqual(a, b)
        self.assertEqual(r.direction(a["ci95"]), "on_better")
        self.assertEqual(r.direction([-5.0, 5.0]), "no_difference_shown")
        self.assertEqual(r.direction([1.0, 5.0]), "on_worse")

    def test_a_never_connected_reconnect_counts_at_the_limit(self):
        c = cycle(0, "on", "harsh", connected=-1)
        self.assertEqual(r.total_ms(c), r.rc.CYCLE_LIMIT_MS)


class Rules(unittest.TestCase):
    def test_a_better_on_arm_is_recommended(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), slow_on=5, slow_off=40)
            v = r.judge(Path(tmp))
            self.assertTrue(v["complete"], v["incomplete_because"])
            self.assertEqual(v["rules"]["udp-harsh"]["O1_over_limit"]["verdict"], "on_better")
            self.assertEqual(v["rules"]["recommendation"], "recommend")
            self.assertIn("O3_p50", v["rules"]["udp-latency"])

    def test_a_security_defect_blocks_the_recommendation(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), slow_on=5, slow_off=40, defect=True)
            v = r.judge(Path(tmp))
            self.assertGreater(v["rules"]["O4_defects"], 0)
            self.assertEqual(v["rules"]["recommendation"], "do_not_recommend")

    def test_no_difference_is_not_recommended(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), slow_on=20, slow_off=20)
            v = r.judge(Path(tmp))
            self.assertEqual(v["rules"]["udp-harsh"]["O1_over_limit"]["verdict"], "no_difference_shown")
            self.assertEqual(v["rules"]["recommendation"], "do_not_recommend")

    def test_missing_request_evidence_never_hides_a_defect(self):  # Codex: unknown D1 is not "no defect"
        for damage in ("missing", "empty", "no_requests"):
            with tempfile.TemporaryDirectory() as tmp:
                write_run(Path(tmp), slow_on=5, slow_off=40, defect=True)
                log = Path(tmp) / "udp-4301-on-harsh" / "client.log"
                if damage == "missing":
                    log.unlink()
                elif damage == "empty":
                    log.write_text("", encoding="utf-8")
                else:
                    kept = [l for l in log.read_text(encoding="utf-8").splitlines() if "connect_request" not in l]
                    log.write_text("\n".join(kept) + "\n", encoding="utf-8")
                v = r.judge(Path(tmp))
                self.assertNotEqual(v["rules"]["recommendation"], "recommend", damage)
                if damage != "missing":
                    self.assertGreater(v["rules"]["O4_unassessable_reconnects"], 0, damage)

    def test_a_later_request_never_stands_in_for_a_missing_first_one(self):  # Codex
        for keep_retry in (True, False):
            with tempfile.TemporaryDirectory() as tmp:
                write_run(Path(tmp), slow_on=5, slow_off=40, defect=True)  # cycle 0 of udp-4301-on-harsh: a D1
                log = Path(tmp) / "udp-4301-on-harsh" / "client.log"
                lines = [json.loads(l) for l in log.read_text(encoding="utf-8").splitlines() if l.strip()]
                # drop cycle 0's first request (and, unless keep_retry, its retry); later cycles keep theirs
                kept = [e for e in lines if not (e["evt"] == "connect_request" and e["t_ms"] < 10_000.0 and
                                                  (e["t_ms"] == 1400.0 or not keep_retry))]
                log.write_text("\n".join(json.dumps(e) for e in kept) + "\n", encoding="utf-8")
                v = r.judge(Path(tmp))
                self.assertEqual(v["rules"]["O4_unassessable_reconnects"], 1, keep_retry)
                self.assertEqual(v["rules"]["recommendation"], "o4_unassessable", keep_retry)

    def test_a_fresh_rejoin_request_is_not_a_reconnect_request(self):
        c = {"cycle": 0, "log_ms": 0.0, "auth": 1400.0, "connected": 1500.0, "host_drops": [1495.0]}
        self.assertFalse(r.classify(c, [], None)["eligible"])           # no reconnect request: unknown
        self.assertTrue(r.classify(c, [1400.0], None)["d1"])            # present at authentication: D1 judged

    def test_an_incomplete_run_gets_no_verdict(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), drop_batch="gns-3-on-harsh")
            v = r.judge(Path(tmp))
            self.assertFalse(v["complete"])
            self.assertEqual(v["rules"]["recommendation"], "incomplete")
            self.assertEqual(v["rules"]["udp-harsh"]["O1_over_limit"]["verdict"], "incomplete")


if __name__ == "__main__":
    unittest.main()
