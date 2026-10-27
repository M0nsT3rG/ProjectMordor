"""Self-checks for the EXP-044 decision arithmetic: python -I tools/ki35/test_run_timer_probe.py"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_timer_probe as r  # noqa: E402


def cycle(j, arm, profile, connected=1500.0, outcome="ok", tx=(4, 4)):
    return {"cycle": j, "log_ms": j * 10_000.0, "outcome": outcome, "reason": "connect_accept", "notice": 100.0,
            "inner": 600.0, "first_hello": 600.0, "hvr": 800.0, "cookie_hello": 800.0, "auth": connected - 100.0,
            "connected": connected, "first_snapshot": connected + 50, "hellos": 2, "timers": 1, "loop_ms": 1.5,
            "host_auth": connected - 150.0, "host_drops": [], "gate_timeouts": 0, "arm": arm, "profile": profile,
            "host_timers": 1, "client_tx": tx[0], "host_tx": tx[1], "cpu_ms": 100.0 * (j + 1)}


def write_run(root: Path, slow_flat=10, slow_off=30, flat_tx=(4, 4), flat_failures=0, drop_batch="", drop_field=""):
    codes = {}
    for b, s, a, p, n in r.BATCHES:
        nm = r.ab.name(b, s, a, p)
        if nm == drop_batch:
            continue
        d = root / nm
        d.mkdir()
        slow = (slow_flat if a == "flat" else slow_off) if p == "harsh" else 0
        lines = []
        for j in range(n):
            c = cycle(j, a, p, connected=4000.0 if j < slow else 1500.0, tx=flat_tx if a == "flat" else (4, 4),
                      outcome="timeout" if a == "flat" and p == "harsh" and j >= n - flat_failures else "ok")
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
        self.assertEqual(len(r.BATCHES), 16)
        self.assertEqual([a for *_, a, p, n in r.BATCHES[:4]], ["off", "flat", "off", "flat"])  # alternating
        self.assertEqual(sorted({s for b, s, a, p, n in r.BATCHES if b == "udp"}), [4401, 4402, 4403, 4404])

    def test_a_better_flat_arm_within_the_guards_means_growth_matters(self):
        v = judged(slow_flat=5, slow_off=40)
        self.assertTrue(v["complete"], v["incomplete_because"])
        self.assertEqual(v["rules"]["udp-harsh"]["O1_over_limit"]["verdict"], "flat_better")
        self.assertEqual(v["rules"]["reading"], "growth_matters")
        self.assertIn("O3_p50_dtls", v["rules"]["gns-latency"])

    def test_no_difference_means_the_cap_is_not_the_lever(self):
        v = judged(slow_flat=20, slow_off=20)
        self.assertEqual(v["rules"]["udp-harsh"]["O1_over_limit"]["verdict"], "no_difference_shown")
        self.assertEqual(v["rules"]["reading"], "timer_cap_not_the_lever")

    def test_a_worse_flat_arm_is_never_read_as_growth_matters(self):
        v = judged(slow_flat=40, slow_off=5)
        self.assertEqual(v["rules"]["udp-harsh"]["O1_over_limit"]["verdict"], "flat_worse")
        self.assertEqual(v["rules"]["reading"], "timer_cap_not_the_lever")

    def test_the_cost_guard_blocks_a_gain_bought_with_datagrams(self):
        v = judged(slow_flat=5, slow_off=40, flat_tx=(7, 6))  # 13 / 8 = 1.625 > 1.5
        self.assertEqual(v["rules"]["udp-harsh"]["C1_datagram_ratio"], 1.625)
        self.assertFalse(v["rules"]["guards"]["C1_cost"])
        self.assertEqual(v["rules"]["reading"], "timer_cap_not_the_lever")

    def test_the_robustness_guard_blocks_extra_timeouts(self):
        v = judged(slow_flat=5, slow_off=40, flat_failures=1)
        self.assertFalse(v["rules"]["guards"]["C2_robustness"])
        self.assertEqual(v["rules"]["reading"], "timer_cap_not_the_lever")

    def test_a_missing_batch_or_field_gives_no_reading(self):
        for kw in ({"drop_batch": "gns-2-flat-harsh"}, {"drop_field": "host_tx"}):
            v = judged(slow_flat=5, slow_off=40, **kw)
            self.assertFalse(v["complete"])
            self.assertEqual(v["rules"]["reading"], "incomplete")

    def test_cpu_counts_only_differences_between_cycles(self):
        s = r.summarize([cycle(j, "off", "harsh") for j in range(3)])
        self.assertEqual(s["cpu_ms_per_cycle"], 100.0)
        self.assertEqual(s["datagrams_per_reconnect"], 8.0)
        self.assertEqual(s["timers_per_reconnect"], 2.0)


if __name__ == "__main__":
    unittest.main(verbosity=1)
