"""Self-checks for the EXP-047 decision arithmetic: python -I tools/ki35/test_run_cap_probe.py"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_cap_probe as r  # noqa: E402


def cycle(j, arm, profile, connected=1500.0, outcome="ok", tx_bytes=(600, 600)):
    return {"cycle": j, "log_ms": j * 10_000.0, "outcome": outcome, "reason": "connect_accept", "notice": 100.0,
            "inner": 600.0, "first_hello": 600.0, "hvr": 800.0, "cookie_hello": 800.0, "auth": connected - 100.0,
            "connected": connected, "first_snapshot": connected + 50, "hellos": 2, "timers": 1, "loop_ms": 1.5,
            "host_auth": connected - 150.0, "host_drops": [], "gate_timeouts": 0, "arm": arm, "profile": profile,
            "host_timers": 1, "client_tx": 4, "host_tx": 4, "cpu_ms": 100.0 * (j + 1),
            "client_tx_bytes": tx_bytes[0], "host_tx_bytes": tx_bytes[1], "client_rx_bytes": 500,
            "host_rx_bytes": 500, "client_resent": 1, "host_resent": 1}


def write_run(root: Path, slow=None, tx_bytes=None, failures=None, slow_path=None, drop_batch="", drop_field=""):
    """slow[arm] (or slow[arm][backend]): harsh reconnects over 3 s per batch; tx_bytes[arm]: bytes per side;
    failures[arm]: timeouts at the end of each harsh batch; slow_path[arm]: total ms of every slow-profile reconnect."""
    slow = {"off": 30, "cap750": 30, "cap500": 30, **(slow or {})}
    tx_bytes, failures, slow_path = tx_bytes or {}, failures or {}, slow_path or {}
    codes = {}
    for b, s, a, p, n in r.BATCHES:
        nm = r.ab.name(b, s, a, p)
        if nm == drop_batch:
            continue
        d = root / nm
        d.mkdir()
        k = slow[a][b] if isinstance(slow[a], dict) else slow[a]
        lines = []
        for j in range(n):
            fail = p == "harsh" and j >= n - failures.get(a, 0)
            conn = (4000.0 if j < k else 1500.0) if p == "harsh" else slow_path.get(a, 2000.0) + j
            c = cycle(j, a, p, connected=conn, outcome="timeout" if fail else "ok", tx_bytes=tx_bytes.get(a, (600, 600)))
            if drop_field and j == 0:
                c.pop(drop_field)
            lines.append(json.dumps(c))
        (d / "cycles.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
        (d / "client.log").write_text("", encoding="utf-8")
        codes[nm] = 0
    (root / "run.json").write_text(json.dumps({"batches": r.BATCHES, "exit_codes": codes}), encoding="utf-8")


def judged(**kw):
    with tempfile.TemporaryDirectory() as tmp:
        write_run(Path(tmp), **kw)
        return r.judge(Path(tmp))


class Rules(unittest.TestCase):
    def test_the_batches_are_the_predeclared_ones(self):
        self.assertEqual(len(r.BATCHES), 24)
        self.assertEqual([a for *_, a, p, n in r.BATCHES[:9]],
                         ["off", "cap750", "cap500", "cap750", "cap500", "off", "cap500", "off", "cap750"])
        self.assertEqual(sorted({s for b, s, a, p, n in r.BATCHES if b == "udp"}), [4701, 4702, 4703, 4704])
        self.assertEqual(sum(n for *_, n in r.BATCHES), 2400)
        self.assertEqual({p for *_, p, n in r.BATCHES}, {"harsh", "slow"})

    def test_a_better_candidate_within_the_guards_improves_the_tail(self):
        v = judged(slow={"off": 40, "cap750": 5, "cap500": 40})
        self.assertTrue(v["complete"], v["incomplete_because"])
        self.assertEqual(v["rules"]["cap750"]["udp-harsh"]["O1_over_limit"]["verdict"], "better")
        self.assertEqual(v["rules"]["reading"], {"cap750": "improves_the_tail", "cap500": "not_shown"})
        self.assertIn("O3_p50_dtls", v["rules"]["cap500"]["gns-slow"])

    def test_a_worse_backend_blocks_the_reading(self):
        v = judged(slow={"off": {"udp": 40, "gns": 5}, "cap750": {"udp": 5, "gns": 40}})
        self.assertEqual(v["rules"]["cap750"]["gns-harsh"]["O1_over_limit"]["verdict"], "worse")
        self.assertEqual(v["rules"]["reading"]["cap750"], "not_shown")

    def test_the_byte_guard_is_exact_at_its_limit(self):
        ok = judged(slow={"off": 40, "cap500": 5}, tx_bytes={"cap500": (900, 600)})  # 1500 / 1200 = 1.25
        self.assertTrue(ok["rules"]["cap500"]["guards"]["G1"])
        self.assertEqual(ok["rules"]["reading"]["cap500"], "improves_the_tail")
        bad = judged(slow={"off": 40, "cap500": 5}, tx_bytes={"cap500": (901, 600)})  # 1501 / 1200
        self.assertEqual(bad["rules"]["cap500"]["udp-harsh"]["G1_bytes_ratio"], 1.2508)
        self.assertFalse(bad["rules"]["cap500"]["guards"]["G1"])
        self.assertEqual(bad["rules"]["reading"]["cap500"], "not_shown")

    def test_extra_timeouts_block_the_reading(self):
        v = judged(slow={"off": 40, "cap750": 5}, failures={"cap750": 1})
        self.assertFalse(v["rules"]["cap750"]["guards"]["G2"])
        self.assertEqual(v["rules"]["reading"]["cap750"], "not_shown")

    def test_a_slower_benign_path_blocks_the_reading(self):
        v = judged(slow={"off": 40, "cap750": 5}, slow_path={"cap750": 2600.0})
        self.assertEqual(v["rules"]["cap750"]["udp-slow"]["G3_p95"]["verdict"], "worse")
        self.assertFalse(v["rules"]["cap750"]["guards"]["G3"])
        self.assertEqual(v["rules"]["reading"]["cap750"], "not_shown")

    def test_a_missing_batch_or_byte_field_gives_no_reading(self):
        for kw in ({"drop_batch": "gns-2-cap500-harsh"}, {"drop_field": "host_rx_bytes"}):
            v = judged(slow={"off": 40, "cap750": 5}, **kw)
            self.assertFalse(v["complete"])
            self.assertEqual(v["rules"]["reading"], {"cap750": "incomplete", "cap500": "incomplete"})

    def test_byte_and_resend_invariants(self):
        base = cycle(0, "cap500", "harsh")
        self.assertTrue(r.traced(base))
        for bad in ({"client_tx_bytes": 3}, {"host_resent": 5}, {"client_rx_bytes": -1}, {"host_tx_bytes": 1.5},
                    {"client_resent": True}, {"host_tx": 0}):
            self.assertFalse(r.traced({**base, **bad}), bad)
        with tempfile.TemporaryDirectory() as tmp:  # a missing datagram size is recorded as -1 bytes: no reading
            write_run(Path(tmp), slow={"off": 40, "cap750": 5})
            p = Path(tmp) / "udp-4704-cap750-slow" / "cycles.jsonl"
            cs = [json.loads(l) for l in p.read_text(encoding="utf-8").splitlines()]
            cs[3]["host_tx_bytes"] = -1
            p.write_text("\n".join(json.dumps(c) for c in cs) + "\n", encoding="utf-8")
            self.assertEqual(r.judge(Path(tmp))["rules"]["reading"]["cap750"], "incomplete")

    def test_per_side_costs_are_reported(self):
        s = judged()["pooled"]["udp-harsh"]["off"]
        self.assertEqual(s["bytes_sent_per_reconnect"], 1200.0)
        self.assertEqual(s["per_side"]["client"], {"bytes_sent": 600.0, "bytes_received": 500.0, "datagrams": 4.0,
                                                   "resent": 1.0, "timer_fires": 1.0})


if __name__ == "__main__":
    unittest.main(verbosity=1)
