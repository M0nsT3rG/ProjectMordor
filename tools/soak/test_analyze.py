"""Self-checks for analyze.py with synthetic logs: python -I tools/soak/test_analyze.py"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze  # noqa: E402


def write_run(d: Path, *, host_invalid=0, c2_reconnects=1, c2_recovery_ms=800, c1_p99=120.0, exit_c1=0, drift_ticks=0):
    run = {"profile": "test", "impairment": {"latency_ms": 40, "jitter_ms": 20, "loss": 0.05, "duplicate": 0.0},
           "stall_s": 5, "host_timeout_ms": 3000, "stalled_clients": ["client2"],
           "exit_codes": {"host": 0, "client1": exit_c1, "client2": 0}}
    (d / "run.json").write_text(json.dumps(run))
    host = [{"t_ms": 0, "evt": "host_started"},
            {"t_ms": 60000, "evt": "host_stopped", "invalid": host_invalid, "uptime_us": 60_000_000, "tick_rate_hz": 30,
             "world_tick": 1800 + drift_ticks, "skipped_ticks": 0, "shed_inputs": 3, "joins": 2, "reconnects": 1, "timeouts": 1}]
    (d / "host.log").write_text("\n".join(map(json.dumps, host)))

    def summary(p99, reconnects):
        return {"t_ms": 59000, "evt": "summary", "final_state": "connected", "rtt_p99_ms": p99, "reconnects": reconnects,
                "invalid": 0, "bytes_in": 1000, "bytes_out": 1000, "elapsed_ms": 59000}
    (d / "client1.log").write_text(json.dumps(summary(c1_p99, 0)))
    c2 = [{"t_ms": 20000, "evt": "stall_begin"}, {"t_ms": 25000, "evt": "stall_end"},
          {"t_ms": 25000 + c2_recovery_ms, "evt": "reconnected"}, summary(110.0, c2_reconnects)]
    (d / "client2.log").write_text("\n".join(map(json.dumps, c2)))


def failed(result):
    return {g["gate"] for g in result["gates"] if not g["passed"]}


class AnalyzeTests(unittest.TestCase):
    def run_case(self, **kw):
        with tempfile.TemporaryDirectory() as d:
            write_run(Path(d), **kw)
            return analyze.analyze(Path(d))

    def test_healthy_run_passes(self):
        r = self.run_case()
        self.assertTrue(r["passed"], failed(r))
        self.assertEqual(r["clients"]["client2"]["recovery_after_stall_ms"], 800)

    def test_each_gate_can_fail(self):
        self.assertIn("host_invalid_packets_zero", failed(self.run_case(host_invalid=1)))
        self.assertIn("client2_reconnected_once", failed(self.run_case(c2_reconnects=2)))
        self.assertIn("client2_recovery_time", failed(self.run_case(c2_recovery_ms=3500)))
        self.assertIn("client1_rtt_p99", failed(self.run_case(c1_p99=200.0)))  # limit = 2*(40+20)+60 = 180
        self.assertIn("client1_exit_code", failed(self.run_case(exit_c1=1)))
        self.assertIn("host_tick_drift", failed(self.run_case(drift_ticks=40)))  # 40/1800 > 1 %

    def test_missing_summary_fails_not_passes(self):
        with tempfile.TemporaryDirectory() as d:
            write_run(Path(d))
            (Path(d) / "client1.log").write_text("not json\n")
            r = analyze.analyze(Path(d))
            self.assertFalse(r["passed"])
            self.assertIn("client1_summary_present", failed(r))


if __name__ == "__main__":
    unittest.main()
