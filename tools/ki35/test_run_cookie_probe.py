"""Self-checks for the EXP-049 decision arithmetic: python -I tools/ki35/test_run_cookie_probe.py"""
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_cookie_probe as r  # noqa: E402


def cycle(j, profile, connected=1500.0, wait=0.0, hello0=1, attempts=1, outcome="ok", **kw):
    c = {"cycle": j, "log_ms": j * 10_000.0, "outcome": outcome, "reason": "connect_accept", "arm": "off",
         "profile": profile, "notice": 100.0, "first_hello": 300.0, "attempts": attempts, "attempt_hello": 300.0,
         "last_hello0": 300.0 + wait, "attempt_cookie_hello": 500.0 + wait, "attempt_auth": connected - 200.0,
         "connected": connected, "hello0_tx": hello0, "host_hello0_rx": hello0, "host_hvr_tx": hello0}
    c.update(kw)
    return c


def write_run(root: Path, slow=30, rescued=10, bad=0, drop_batch="", drop_field="", hetero=False):
    """Per harsh batch: `slow` reconnects at 3400 ms, the first `rescued` of them with 500 ms of cookie wait (so their
    total minus the wait is 2900 ms); `bad` unattributable reconnects; hetero puts every slow reconnect in batch 1."""
    codes = {}
    for i, (b, s, a, p, n) in enumerate(r.BATCHES):
        nm = r.ab.name(b, s, a, p)
        if nm == drop_batch:
            continue
        d = root / nm
        d.mkdir()
        k = (slow * 4 if s in (4901, 1) else 0) if hetero else slow
        lines = []
        for j in range(n):
            if p == "harsh" and j < k:
                c = cycle(j, p, connected=3400.0, wait=500.0 if j < rescued else 0.0, hello0=3 if j < rescued else 1)
            elif p == "harsh" and j < k + bad:
                c = cycle(j, p, attempt_auth=-1.0)
            else:
                c = cycle(j, p)
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
        self.assertEqual(len(r.BATCHES), 12)
        self.assertEqual({a for _, _, a, _, _ in r.BATCHES}, {"off"})
        self.assertEqual([s for b, s, a, p, n in r.BATCHES if b == "udp"], [4901, 4902, 4903, 4904, 4905, 4906])
        self.assertEqual([s for b, s, a, p, n in r.BATCHES if b == "gns"], [1, 2, 3, 4, 5, 6])
        self.assertEqual(sum(n for *_, n in r.BATCHES), 1200)

    def test_homogeneity_with_four_batches(self):
        # chi-square(3) at 7.8147 has survival 0.05; equal batches give 1.
        x2_counts = [(30, 100), (30, 100), (30, 100), (30, 100)]
        self.assertAlmostEqual(r.rc.homogeneity_p(x2_counts, exact=True), 1.0)
        x = 7.8147
        sf = math.erfc(math.sqrt(x / 2)) + math.sqrt(2 * x / math.pi) * math.exp(-x / 2)
        self.assertAlmostEqual(sf, 0.05, places=4)

    def test_phases_add_up_to_total(self):
        c = cycle(0, "harsh", connected=3400.0, wait=500.0)
        self.assertAlmostEqual(sum(r.phases(c).values()), c["connected"])
        self.assertEqual(r.cookie_wait(c), 500.0)

    def test_attributable_needs_an_authenticated_final_attempt_and_order(self):
        self.assertTrue(r.attributable(cycle(0, "harsh")))
        self.assertFalse(r.attributable(cycle(0, "harsh", attempt_auth=-1.0)))
        self.assertFalse(r.attributable(cycle(0, "harsh", attempts=0)))
        self.assertFalse(r.attributable(cycle(0, "harsh", hello0=0)))
        self.assertFalse(r.attributable(cycle(0, "harsh", attempt_hello=200.0)))  # before first_hello
        self.assertFalse(r.attributable(cycle(0, "harsh", last_hello0=600.0)))    # after the cookie

    def test_flight_classes(self):
        self.assertEqual(r.flight(cycle(0, "harsh")), "clean")
        self.assertEqual(r.flight(cycle(0, "harsh", hello0=3, host_hello0_rx=2)), "hello_lost")
        self.assertEqual(r.flight(cycle(0, "harsh", hello0=3, host_hello0_rx=3)), "hvr_lost_or_late")

    def test_thresholds(self):
        self.assertEqual(r.r_wait_verdict(40, 100), "explain_part_of_the_tail")   # [0.309, 0.498]
        self.assertEqual(r.r_wait_verdict(10, 100), "do_not_explain_the_tail")    # [0.055, 0.174]
        self.assertEqual(r.r_wait_verdict(25, 100), "inconclusive")
        self.assertEqual(r.r_wait_verdict(29, 29), "not_assessable")

    def test_gns_explains_leads_to_a_cookie_experiment(self):
        v = judged(slow=30, rescued=15)  # 60 of 120 rescued
        self.assertTrue(v["complete"])
        self.assertEqual(v["rules"]["gns"]["R_wait_verdict"], "explain_part_of_the_tail")
        self.assertEqual(v["rules"]["reading"]["next"], "predeclare_a_behavioural_cookie_experiment")
        self.assertEqual(v["pooled"]["gns-harsh"]["R_wait"]["k"], 60)

    def test_gns_does_not_explain_returns_to_establishment(self):
        v = judged(slow=30, rescued=2)  # 8 of 120
        self.assertEqual(v["rules"]["reading"], {"verdict": "do_not_explain_the_tail", "next": "return_to_gns_establishment"})

    def test_too_few_slow_reconnects(self):
        v = judged(slow=7, rescued=7)  # 28 of 28
        self.assertEqual(v["rules"]["gns"]["R_wait_verdict"], "not_assessable")
        self.assertEqual(v["rules"]["reading"]["next"], "owner_and_reviewer_choose")

    def test_unattributable_over_ten_percent(self):
        self.assertEqual(judged(bad=10, rescued=2)["rules"]["gns"]["R_wait_verdict"], "do_not_explain_the_tail")  # 10%
        self.assertEqual(judged(bad=11, rescued=2)["rules"]["gns"]["R_wait_verdict"], "not_assessable")

    def test_heterogeneous_batches_have_no_pooled_reading(self):
        self.assertEqual(judged(hetero=True)["rules"]["gns"]["R_wait_verdict"], "heterogeneous")

    def test_incomplete_runs_have_no_verdict(self):
        v = judged(drop_batch="gns-3-off-harsh")
        self.assertFalse(v["complete"])
        self.assertEqual(v["rules"]["reading"]["verdict"], "incomplete")
        self.assertFalse(judged(drop_field="outcome")["complete"])

    def test_a_missing_attempt_field_only_makes_that_reconnect_unattributable(self):
        v = judged(drop_field="attempt_auth", rescued=2)
        self.assertTrue(v["complete"])
        self.assertEqual(v["pooled"]["gns-harsh"]["unattributable"], 4)
        self.assertEqual(v["rules"]["gns"]["R_wait_verdict"], "do_not_explain_the_tail")


if __name__ == "__main__":
    unittest.main()
