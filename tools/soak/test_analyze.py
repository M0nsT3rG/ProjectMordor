"""Self-checks for analyze.py with synthetic logs: python -I tools/soak/test_analyze.py"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze  # noqa: E402


def summary(p99=110.0, reconnects=0, kin=20.0, kout=25.0, max_corr=0.3, samples=400, elapsed_ms=120_000):
    # kbit/s = bytes * 8 / elapsed_ms  =>  bytes = kbit/s * elapsed_ms / 8
    return {"t_ms": 119_000, "evt": "summary", "final_state": "connected", "rtt_p99_ms": p99, "reconnects": reconnects,
            "invalid": 0, "bytes_in": int(kin * elapsed_ms / 8), "bytes_out": int(kout * elapsed_ms / 8),
            "elapsed_ms": elapsed_ms, "rtt_samples": samples, "max_correction": max_corr}


def write_run(d: Path, *, host_invalid=0, c2_reconnects=1, c2_recovery_ms=800, c1=None, exit_c1=0, drift_ticks=0,
              expected=("client1", "client2"), logs=("client1", "client2"), manifest=True):
    run = {"profile": "test", "duration_s": 120, "impairment": {"latency_ms": 40, "jitter_ms": 20, "loss": 0.05, "duplicate": 0.0},
           "stall_s": 5, "host_timeout_ms": 3000, "stalled_clients": ["client2"],
           "exit_codes": {"host": 0, "client1": exit_c1, "client2": 0}}
    if manifest:
        run["expected_clients"] = list(expected)
    (d / "run.json").write_text(json.dumps(run))
    host = [{"t_ms": 0, "evt": "host_started"},
            {"t_ms": 126_000, "evt": "host_stopped", "invalid": host_invalid, "uptime_us": 126_000_000, "tick_rate_hz": 30,
             "world_tick": 3780 + drift_ticks, "skipped_ticks": 0, "shed_inputs": 3, "joins": 2, "reconnects": 1, "timeouts": 1}]
    (d / "host.log").write_text("\n".join(map(json.dumps, host)))
    contents = {
        "client1": [c1 or summary()],
        "client2": [{"t_ms": 48_000, "evt": "stall_begin"}, {"t_ms": 53_000, "evt": "stall_end"},
                    {"t_ms": 53_000 + c2_recovery_ms, "evt": "reconnected"}, summary(reconnects=c2_reconnects)],
    }
    for name in logs:
        (d / f"{name}.log").write_text("\n".join(map(json.dumps, contents.get(name, contents["client1"]))))


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
        self.assertEqual(r["gate_version"], 2)
        self.assertEqual(r["clients"]["client2"]["recovery_after_stall_ms"], 800)

    def test_each_v1_gate_can_fail(self):
        self.assertIn("host_invalid_packets_zero", failed(self.run_case(host_invalid=1)))
        self.assertIn("client2_reconnected_once", failed(self.run_case(c2_reconnects=2)))
        self.assertIn("client2_recovery_time", failed(self.run_case(c2_recovery_ms=3500)))
        self.assertIn("client1_rtt_p99", failed(self.run_case(c1=summary(p99=200.0))))  # limit = 2*(40+20)+60 = 180
        self.assertIn("client1_exit_code", failed(self.run_case(exit_c1=1)))
        self.assertIn("host_tick_drift", failed(self.run_case(drift_ticks=80)))  # 80/3780 > 1 %

    def test_missing_summary_fails_not_passes(self):
        with tempfile.TemporaryDirectory() as d:
            write_run(Path(d))
            (Path(d) / "client1.log").write_text("not json\n")
            r = analyze.analyze(Path(d))
            self.assertFalse(r["passed"])
            self.assertIn("client1_summary_present", failed(r))

    # FB-2-01: the manifest, not the directory listing, defines who must be gated.
    def test_missing_client_log_fails(self):
        r = self.run_case(logs=("client1",))  # client2 never produced a log
        self.assertFalse(r["passed"])
        self.assertIn("client_logs_match_manifest", failed(r))
        self.assertIn("client2_summary_present", failed(r))   # still gated although absent
        self.assertIn("client2_reconnected_once", failed(r))  # its stall gates are not skipped

    def test_renamed_client_log_fails(self):
        r = self.run_case(logs=("client1", "client3"))
        self.assertIn("client_logs_match_manifest", failed(r))
        self.assertIn("client2_summary_present", failed(r))

    def test_extra_client_log_fails(self):
        r = self.run_case(logs=("client1", "client2", "client3"))
        self.assertIn("client_logs_match_manifest", failed(r))

    def test_missing_manifest_fails(self):
        r = self.run_case(manifest=False)
        self.assertIn("manifest_lists_expected_clients", failed(r))
        self.assertFalse(r["passed"])

    def test_joins_compare_to_manifest(self):
        r = self.run_case(expected=("client1", "client2", "client3"), logs=("client1", "client2", "client3"))
        self.assertIn("host_joins_match_manifest", failed(r))  # host saw 2 joins, manifest expects 3

    # FB-2-03: design-derived ceilings and sample minimums.
    def test_bandwidth_correction_and_coverage_gates_can_fail(self):
        self.assertIn("client1_download_budget", failed(self.run_case(c1=summary(kin=30.0))))
        self.assertIn("client1_upload_budget", failed(self.run_case(c1=summary(kout=40.0))))
        self.assertIn("client1_correction_budget", failed(self.run_case(c1=summary(max_corr=2.0))))
        self.assertIn("client1_rtt_sample_count", failed(self.run_case(c1=summary(samples=100))))
        self.assertIn("client1_ran_full_duration", failed(self.run_case(c1=summary(elapsed_ms=60_000))))

    def test_budgets_follow_protocol_arithmetic(self):
        # 87-byte snapshot x 30 Hz + 30-byte ack x 4 Hz = 2730 B/s = 21.84 kbit/s, x1.05 margin
        self.assertAlmostEqual(analyze.download_budget_kbit(2, 0.0), 22.932, places=3)
        # 119-byte input batch x 30 Hz + 30-byte heartbeat x 4 Hz = 3690 B/s = 29.52 kbit/s, x1.05 margin
        self.assertAlmostEqual(analyze.upload_budget_kbit(), 30.996, places=3)
        self.assertAlmostEqual(analyze.correction_budget(180.0), 5.0 * (0.18 + 0.1), places=6)

    def test_existing_summary_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as d:
            write_run(Path(d))
            (Path(d) / "summary.json").write_text('{"original": true}')
            self.assertEqual(analyze.main([d]), 2)
            self.assertEqual(json.loads((Path(d) / "summary.json").read_text()), {"original": True})


if __name__ == "__main__":
    unittest.main()
