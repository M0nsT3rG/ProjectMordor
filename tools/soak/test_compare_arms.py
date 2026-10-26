"""Self-checks for compare_arms.py's decision rule: python -I tools/soak/test_compare_arms.py"""
import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_arms as c  # noqa: E402


def run(rec, failed=()):
    return {"recovery": rec, "failed": set(failed)}


class DecisionRule(unittest.TestCase):
    def test_b_adopted_when_fewer_failures_and_10pct_faster(self):
        g = {"p": {500: [run(3100, ["client2_recovery_time"]), run(2000), run(2500)], 250: [run(1500), run(1800), run(2000)]}}
        self.assertTrue(c.verdict(g)[0])

    def test_new_other_gate_failure_rejects_b(self):
        g = {"p": {500: [run(2000)], 250: [run(1000, ["host_joins_match_manifest"])]}}
        self.assertFalse(c.verdict(g)[0])

    def test_less_than_10pct_improvement_rejects_b(self):
        self.assertFalse(c.verdict({"p": {500: [run(2000)], 250: [run(1900)]}})[0])

    def test_more_recovery_failures_rejects_b(self):
        g = {"p": {500: [run(2000), run(2000)], 250: [run(100), run(3100, ["client2_recovery_time"])]}}
        self.assertFalse(c.verdict(g)[0])

    def test_every_profile_must_pass(self):
        g = {"p": {500: [run(2000)], 250: [run(1000)]}, "q": {500: [run(2000)], 250: [run(1950)]}}
        self.assertFalse(c.verdict(g)[0])

    def test_never_recovered_run_counts_as_worst(self):
        # A stalled client that never reconnects has no recovery value (None in summary.json): load() makes it inf.
        g = {"p": {500: [run(2000), run(2100), run(2200)],
                   250: [run(1000), run(1000), run(math.inf, ["client2_recovery_time"])]}}
        self.assertFalse(c.verdict(g)[0])  # one more failure than A: never adopted, and no crash
        self.assertEqual(c.ms(math.inf), "none")
        self.assertEqual(c.ms(None), "none")

    def test_load_turns_missing_recovery_into_inf(self):
        import json, tempfile
        s = {"run": {"profile": "harsh", "transport": "gns", "secure": True, "reconnect_retry_ms": 250},
             "gates": [{"gate": "client2_recovery_time", "passed": False}],
             "clients": {"client2": {"recovery_after_stall_ms": None, "rtt_p50_ms": None}},
             "distributions": {"delivery_ratio": {"client2": None}}}
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "summary.json").write_text(json.dumps(s), encoding="utf-8")
            r = c.load(Path(d))
        self.assertEqual((r["recovery"], r["retry"], r["key"]), (math.inf, 250, "harsh-gns-sec"))

    def test_boolean_arm_field(self):
        import json, tempfile
        s = {"run": {"profile": "harsh", "transport": "gns", "secure": True, "skip_cookie_if_routable": True},
             "gates": [], "clients": {"client2": {"recovery_after_stall_ms": 1500, "rtt_p50_ms": 190}},
             "distributions": {"delivery_ratio": {"client2": 0.84}}}
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "summary.json").write_text(json.dumps(s), encoding="utf-8")
            c.ARM_FIELD = "skip_cookie_if_routable"
            try:
                self.assertEqual(c.load(Path(d))["retry"], c.B_MS)  # true -> arm B
            finally:
                c.ARM_FIELD = None

    def test_missing_arm_rejects_b(self):
        self.assertFalse(c.verdict({"p": {500: [run(2000)]}})[0])

    def test_no_usable_runs_rejects_b(self):
        # FB-16-02: every run aborted leaves no group; that must never read as an adoption.
        self.assertFalse(c.verdict({})[0])

    def test_all_aborted_cli_gives_no_decision(self):
        import contextlib, io
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = c.main(["__no_such_run_dir__"])
        self.assertEqual(code, 1)
        self.assertIn("VERDICT: no decision", out.getvalue())
        self.assertNotIn("adopt B", out.getvalue())

    def test_never_recovered_in_both_arms_rejects_b(self):
        # FB-16-02: inf <= 0.9 * inf is True; neither arm recovered, so B cannot have improved.
        f = run(math.inf, ["client2_recovery_time"])
        self.assertFalse(c.verdict({"p": {500: [f], 250: [f]}})[0])
        self.assertFalse(c.verdict({"p": {500: [f] * 5, 250: [f] * 5}})[0])

    def test_finite_b_against_never_recovered_a_still_judged(self):
        # B recovered where A never did: a finite B median is measurable, so the rule can still adopt it.
        g = {"p": {500: [run(math.inf, ["client2_recovery_time"])] * 3, 250: [run(1500)] * 3}}
        self.assertTrue(c.verdict(g)[0])


if __name__ == "__main__":
    unittest.main()
