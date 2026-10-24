"""Self-checks for check_secure.py: run with `python -I tools/e2e/test_check_secure.py`."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_secure as cs  # noqa: E402


def lines(*evts):
    return "\n".join(json.dumps(e) for e in evts) + "\n"


def write_good(d: Path, **over):
    run = {"transport": "gns", "exit_codes": {"host": 0, "client1": 0, "client2": 0, "host2": 0},
           "negative_exit_codes": {"plaintext": 1, "stranger": 1, "wrong_invitation": 1, "expired_invitation": 2, "missing_key": 2}}
    run.update(over.get("run", {}))
    (d / "run.json").write_text(json.dumps(run))
    sec = {"evt": "security", "mode": "dtls"}
    (d / "host.log").write_text(lines(sec, {"evt": "host_stopped", "timeouts": over.get("timeouts", 0)},
                                      {"evt": "secure_stats", "authenticated": 2, "dropped_not_dtls": over.get("dropped", 0)}))
    (d / "host2.log").write_text(lines(sec, {"evt": "secure_stats", "authenticated": 0, "dropped_not_dtls": 3}))
    for n in ("client1", "client2"):
        (d / f"{n}.log").write_text(lines(over.get("client_sec", sec), {"evt": "summary", "final_state": "connected", "snapshots": 200}))
    (d / "expired_invitation.stderr.txt").write_text("error: secure mode: the invitation has expired\n")
    (d / "missing_key.stderr.txt").write_text("error: secure mode: key file not found\n")
    if over.get("leak"):
        (d / "plaintext.log").write_text("something MRDRKEY1 something\n")


class SecureCheckTests(unittest.TestCase):
    def failed(self, **over):
        with tempfile.TemporaryDirectory() as t:
            d = Path(t)
            write_good(d, **over)
            r = cs.check(d)
            return {g["gate"] for g in r["gates"] if not g["passed"]}

    def test_good_run_passes(self):
        self.assertEqual(self.failed(), set())

    def test_violations_are_caught(self):
        self.assertIn("host_no_plaintext_datagrams", self.failed(dropped=1))
        self.assertIn("host_no_timeouts", self.failed(timeouts=1))
        self.assertIn("client1_security_dtls", self.failed(client_sec={"evt": "security", "mode": "plaintext"}))
        self.assertIn("negative_plaintext_cannot_connect", self.failed(run={"negative_exit_codes": {
            "plaintext": 0, "stranger": 1, "wrong_invitation": 1, "expired_invitation": 2, "missing_key": 2}}))
        self.assertIn("no_key_material_or_key_paths_in_logs", self.failed(leak=True))


if __name__ == "__main__":
    unittest.main()
