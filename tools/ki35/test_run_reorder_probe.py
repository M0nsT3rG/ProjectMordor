"""Self-checks for the EXP-042 decision arithmetic: python -I tools/ki35/test_run_reorder_probe.py"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_reorder_probe as r  # noqa: E402


def cycle(**over):  # log_ms 1000, auth at 1400 (log 2400); the first request is logged at 2400
    c = {"cycle": 0, "log_ms": 1000.0, "outcome": "ok", "reason": "connect_accept", "notice": 100.0, "inner": 600.0,
         "first_hello": 600.0, "hvr": 800.0, "cookie_hello": 800.0, "auth": 1400.0, "connected": 2100.0,
         "first_snapshot": 2150.0, "hellos": 2, "timers": 0, "loop_ms": 1.5, "host_auth": 1505.0, "host_drops": [],
         "attribution": {"attributed": True, "index": 0, "requests": 1}}
    c.update(over)
    return c


class Classify(unittest.TestCase):
    def test_a_drop_one_way_after_the_first_request_is_d1(self):
        x = r.classify(cycle(host_drops=[1495.0], attribution={"attributed": True, "index": 1}), [2400.0, 2900.0])
        self.assertTrue(x["d1"])
        self.assertFalse(x["first"])
        self.assertEqual(x["binding_after_drop_ms"], 10.0)

    def test_a_later_drop_is_not_d1(self):  # the second request (sent 500 ms later) dropped: not the first
        self.assertFalse(r.classify(cycle(host_drops=[1995.0]), [2400.0, 2900.0])["d1"])

    def test_no_first_request_is_not_eligible(self):
        self.assertFalse(r.classify(cycle(auth=-1), [2400.0])["eligible"])

    def test_unattributed_cycles_count_for_d1_only(self):
        cs = [cycle(host_drops=[1495.0], attribution={"attributed": False, "why": "ambiguous"})]
        for c in cs:
            c["reorder"] = r.classify(c, [2400.0])
        s = r.series(cs)
        self.assertEqual((s["d1"], s["first"]), ([1], []))


def write_run(root: Path, d1_udp: float, first_given_d1: float, d1_gns: float = 0.2, drop_last: bool = False):
    codes = {}
    for backend, seed in r.BATCHES:
        name = f"{backend}-{seed}"
        d = root / name
        d.mkdir()
        p = d1_udp if backend == "udp" else d1_gns
        lines, log = [], []
        n = 99 if drop_last and name == "gns-3" else 100
        for j in range(n):
            base = j * 10_000.0
            dropped = (j % 100) < round(p * 100)
            # first given D1: the first `first_given_d1` share of dropped cycles still completes on the first request
            k_d1 = round(p * 100)
            first = ((j % 100) < round(first_given_d1 * k_d1)) if dropped else ((j % 100) % 5 != 0)
            lines.append(json.dumps(cycle(cycle=j, log_ms=base, host_drops=[1495.0] if dropped else [],
                                          connected=1590.0 if first else 2090.0)))
            log.append(json.dumps({"t_ms": base + 1400.0, "evt": "connect_request", "sent": True}))
            if not first:
                log.append(json.dumps({"t_ms": base + 1900.0, "evt": "connect_request", "sent": True}))
            log.append(json.dumps({"t_ms": base + (1590.0 if first else 2090.0), "evt": "connect_accept_received"}))
        (d / "cycles.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
        (d / "client.log").write_text("\n".join(log) + "\n", encoding="utf-8")
        codes[name] = 0
    (root / "run.json").write_text(json.dumps({"cycles_per_batch": 100, "batches": r.BATCHES, "exit_codes": codes}),
                                   encoding="utf-8")


class Rules(unittest.TestCase):
    def test_h_is_refuted_when_drops_look_in_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), d1_udp=0.13, first_given_d1=0.0)
            v = r.judge(Path(tmp))
            self.assertTrue(v["complete"], v["incomplete_because"])
            self.assertFalse(v["rules"]["udp"]["d1_differs_from_in_order"])
            self.assertEqual(v["rules"]["udp"]["H"], "refuted")

    def test_gns_directional_rules(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), d1_udp=0.49, first_given_d1=0.0, d1_gns=0.15)
            v = r.judge(Path(tmp))
            self.assertTrue(v["rules"]["gns"]["d1_below_udp"])
            self.assertTrue(v["rules"]["gns"]["dropped_first_rarely_completes"])
            self.assertTrue(v["rules"]["udp"]["d1_differs_from_in_order"])
            self.assertEqual(v["rules"]["udp"]["d1"]["verdict"], "equivalent")

    def test_incomplete_run_has_no_verdict(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), d1_udp=0.49, first_given_d1=0.0, drop_last=True)
            v = r.judge(Path(tmp))
            self.assertEqual(v["rules"]["udp"]["H"], "incomplete")
            self.assertEqual(v["rules"]["gns"], "incomplete")


if __name__ == "__main__":
    unittest.main()
