"""Self-checks for the EXP-037 decision arithmetic: python -I tools/ki35/test_run_secure_probe.py"""
import json
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


class MissingLog(unittest.TestCase):
    def test_a_missing_client_log_makes_session_first_unavailable_not_zero(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp) / "udp-1"
            d.mkdir()
            t = {"trial": 0, "log_ms": 0, "ok": True, "inner": -1, "first_hello": 1, "auth": 500, "connected": 700,
                 "hellos": 2, "timers": 0}
            (d / "trials.jsonl").write_text(json.dumps(t) + "\n", encoding="utf-8")
            v = r.judge(Path(tmp))
            self.assertEqual(v["pooled"]["udp"]["proportions"]["session_first"]["n"], 0)
            self.assertFalse(v["rules"]["udp"]["session_first"]["available"])
            (d / "client.log").write_text('{"t_ms":500,"evt":"connect_request"}\n', encoding="utf-8")
            self.assertEqual(r.judge(Path(tmp))["pooled"]["udp"]["proportions"]["session_first"]["k"], 1)


class Bounded(unittest.TestCase):
    def test_a_hung_probe_keeps_its_output_and_is_dumped_then_killed(self):
        import tempfile
        from bounded import run_bounded
        dumped = []
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "trials.jsonl"
            child = 'import time; print("{\\"trial\\":0}", flush=True); print("{\\"tri", end="", flush=True); time.sleep(60)'
            code = run_bounded([sys.executable, "-I", "-c", child], out, timeout_s=2, dump=lambda pid, t: dumped.append(pid))
            self.assertEqual(code, "hung")
            self.assertEqual(len(dumped), 1)
            d = Path(tmp)
            self.assertEqual(r.load_trials(d), [{"trial": 0}])  # the cut last line is dropped, the finished one kept

    def test_a_normal_exit_returns_its_code(self):
        import tempfile
        from bounded import run_bounded
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(run_bounded([sys.executable, "-I", "-c", "raise SystemExit(3)"], Path(tmp) / "o", 30), 3)


if __name__ == "__main__":
    unittest.main()
