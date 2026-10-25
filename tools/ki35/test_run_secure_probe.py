"""Self-checks for the EXP-037 decision arithmetic: python -I tools/ki35/test_run_secure_probe.py"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_secure_probe as r  # noqa: E402


class Arithmetic(unittest.TestCase):
    def test_wilson_matches_known_values(self):
        self.assertEqual(r.wilson(50, 100), [0.4038, 0.5962])  # the textbook 95% Wilson interval for 50/100
        self.assertEqual(r.wilson(0, 10)[0], 0.0)
        self.assertIsNone(r.wilson(0, 0))

    def test_overlap_is_inclusive_and_symmetric(self):
        self.assertTrue(r.overlaps([0.3, 0.4], [0.4, 0.5]))
        self.assertTrue(r.overlaps([0.4, 0.5], [0.3, 0.4]))
        self.assertFalse(r.overlaps([0.1, 0.2], [0.3, 0.4]))

    def test_supported_needs_the_interval_to_reach_the_model(self):
        ci = r.wilson(70, 100)  # about [0.604, 0.781]
        self.assertTrue(r.overlaps(ci, r.MODEL["session_first"]))
        self.assertFalse(r.overlaps(ci, r.MODEL["dtls_no_timer"]))


if __name__ == "__main__":
    unittest.main()
