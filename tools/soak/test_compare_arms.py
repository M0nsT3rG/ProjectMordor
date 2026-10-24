"""Self-checks for compare_arms.py's decision rule: python -I tools/soak/test_compare_arms.py"""
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

    def test_missing_arm_rejects_b(self):
        self.assertFalse(c.verdict({"p": {500: [run(2000)]}})[0])


if __name__ == "__main__":
    unittest.main()
