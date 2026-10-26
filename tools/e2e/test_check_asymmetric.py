"""Self-checks for check_asymmetric.py: run with `python -I tools/e2e/test_check_asymmetric.py`."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_asymmetric as ca  # noqa: E402


def write_run(d: Path, *, timeouts=1, c1_reconnects=0, c2_reconnects=1, c2_player=2, c2_max_ms=900, rtt1=25.0,
              scenario="host-jitter-client-outage"):
    run = {"scenario": scenario, "transport": "gns", "outage_s": 5, "host_impairment": {"latency_ms": 20},
           "exit_codes": {"host": 0, "client1": 0, "client2": 0}}
    (d / "run.json").write_text(json.dumps(run))
    (d / "host.log").write_text(json.dumps({"evt": "host_stopped", "transport": "gns", "timeouts": timeouts}) + "\n")
    (d / "client1.log").write_text("\n".join(json.dumps(e) for e in [
        {"evt": "status", "player": 1},
        {"evt": "summary", "final_state": "connected", "player": 1, "reconnects": c1_reconnects, "rtt_p50_ms": rtt1}]))
    (d / "client2.log").write_text("\n".join(json.dumps(e) for e in [
        {"evt": "status", "player": 2}, {"evt": "outage_end"},
        {"evt": "summary", "final_state": "connected", "player": c2_player, "reconnects": c2_reconnects,
         "max_reconnect_ms": c2_max_ms}]))


class AsymmetricCheckTests(unittest.TestCase):
    def run_case(self, **kw):
        with tempfile.TemporaryDirectory() as t:
            d = Path(t)
            write_run(d, **kw)
            r = ca.check(d)
            return r["passed"], {g["gate"] for g in r["gates"] if not g["passed"]}

    def test_good_run_passes(self):
        self.assertEqual(self.run_case(), (True, set()))

    def test_each_violation_is_caught(self):
        self.assertIn("host_timed_out_exactly_the_cut_client", self.run_case(timeouts=2)[1])
        self.assertIn("client1_never_reconnected", self.run_case(c1_reconnects=1)[1])
        self.assertIn("client2_reconnected", self.run_case(c2_reconnects=0)[1])
        self.assertIn("client2_kept_identity", self.run_case(c2_player=3)[1])
        self.assertIn("client2_reconnect_bounded", self.run_case(c2_max_ms=9001)[1])
        self.assertIn("client1_sees_host_only_delay", self.run_case(rtt1=5.0)[1])

    def test_refuses_to_overwrite(self):
        with tempfile.TemporaryDirectory() as t:
            d = Path(t)
            write_run(d)
            (d / "check.json").write_text("{}")
            sys.argv = ["x", str(d)]
            self.assertEqual(ca.main(), 2)


if __name__ == "__main__":
    unittest.main()
