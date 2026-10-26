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

    def test_unknown_attribution_counts_for_d1_only(self):
        cs = [cycle(host_drops=[1495.0], attribution={"attributed": False, "why": "ambiguous"})]
        for c in cs:
            c["reorder"] = r.classify(c, [2400.0])
        s = r.series(cs)
        self.assertEqual((s["d1"], s["first"]), ([1], []))

    def test_a_session_that_never_completed_is_a_known_failure(self):  # Codex: never silently dropped
        x = r.classify(cycle(connected=-1, outcome="timeout", attribution={"attributed": False, "why": "no_session"}),
                       [2400.0])
        self.assertIs(x["first"], False)


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
            self.assertEqual(v["rules"]["udp"]["in_order_contrast"], "not_different")
            self.assertEqual(v["rules"]["udp"]["d1"]["verdict"], "different")
            self.assertEqual(v["rules"]["udp"]["H"], "refuted")

    def test_no_usable_observations_is_inconclusive_not_refuted(self):  # Codex
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), d1_udp=0.49, first_given_d1=0.0)
            for b in ("udp-4201", "udp-4202", "udp-4203"):
                (Path(tmp) / b / "client.log").write_text("", encoding="utf-8")  # no request logged: none eligible
            v = r.judge(Path(tmp))
            self.assertEqual(v["rules"]["udp"]["in_order_contrast"], "no_data")
            self.assertEqual(v["rules"]["udp"]["H"], "inconclusive")

    def test_heterogeneous_batches_get_per_batch_verdicts_and_no_gns_direction(self):  # Codex
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), d1_udp=0.49, first_given_d1=0.0, d1_gns=0.15)
            d = Path(tmp) / "udp-4203"  # rewrite one UDP batch with far fewer drops
            lines = [json.loads(l) for l in (d / "cycles.jsonl").read_text(encoding="utf-8").splitlines()]
            for c in lines:
                c["host_drops"] = []
            (d / "cycles.jsonl").write_text("\n".join(json.dumps(c) for c in lines) + "\n", encoding="utf-8")
            v = r.judge(Path(tmp))
            self.assertEqual(v["rules"]["udp"]["d1"]["verdict"], "heterogeneous")
            self.assertEqual(set(v["rules"]["udp"]["d1"]["per_batch"]), {"udp-4201", "udp-4202", "udp-4203"})
            self.assertEqual(v["rules"]["gns"]["verdict"], "heterogeneous")

    def test_gns_directional_rules(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), d1_udp=0.49, first_given_d1=0.0, d1_gns=0.15)
            v = r.judge(Path(tmp))
            self.assertTrue(v["rules"]["gns"]["d1_below_udp"])
            self.assertTrue(v["rules"]["gns"]["dropped_first_rarely_completes"])
            self.assertEqual(v["rules"]["udp"]["in_order_contrast"], "different")
            self.assertEqual(v["rules"]["udp"]["d1"]["verdict"], "equivalent")

    def test_a_run_without_the_host_trace_is_incomplete(self):  # Codex: a pre-EXP-042 probe is not "no drops"
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), d1_udp=0.49, first_given_d1=0.0)
            d = Path(tmp) / "gns-2"
            lines = [json.loads(l) for l in (d / "cycles.jsonl").read_text(encoding="utf-8").splitlines()]
            for c in lines:
                del c["host_drops"], c["host_auth"]
            (d / "cycles.jsonl").write_text("\n".join(json.dumps(c) for c in lines) + "\n", encoding="utf-8")
            v = r.judge(Path(tmp))
            self.assertFalse(v["complete"])
            self.assertEqual(v["rules"]["udp"]["H"], "incomplete")

    def test_per_batch_verdicts_respect_the_attribution_gate(self):  # Codex
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), d1_udp=0.49, first_given_d1=0.0)
            d = Path(tmp) / "udp-4203"  # heterogeneous D1 and 70 unknown outcomes in this batch
            lines = [json.loads(l) for l in (d / "cycles.jsonl").read_text(encoding="utf-8").splitlines()]
            for c in lines:
                c["host_drops"] = []
            (d / "cycles.jsonl").write_text("\n".join(json.dumps(c) for c in lines) + "\n", encoding="utf-8")
            log = [json.loads(l) for l in (d / "client.log").read_text(encoding="utf-8").splitlines()]
            accepts = [e for e in log if e["evt"] == "connect_accept_received"]
            drop = {e["t_ms"] for e in accepts[:70]}
            (d / "client.log").write_text("\n".join(json.dumps(e) for e in log if e["t_ms"] not in drop or
                                                     e["evt"] != "connect_accept_received") + "\n", encoding="utf-8")
            v = r.judge(Path(tmp))
            u = v["rules"]["udp"]
            self.assertEqual(u["first"]["verdict"], "not_assessable")  # pooled unknown share 70/300 > 10%
            self.assertEqual(u["d1"]["verdict"], "heterogeneous")

    def test_incomplete_run_has_no_verdict(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), d1_udp=0.49, first_given_d1=0.0, drop_last=True)
            v = r.judge(Path(tmp))
            self.assertEqual(v["rules"]["udp"]["H"], "incomplete")
            self.assertEqual(v["rules"]["gns"], "incomplete")


if __name__ == "__main__":
    unittest.main()
