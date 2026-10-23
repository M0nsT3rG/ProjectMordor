"""Self-checks for report.py: run with `python -I tools/collab/test_report.py`."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import report  # noqa: E402


class ReportTests(unittest.TestCase):
    def test_auto_block_replaced_and_manual_text_kept(self):
        text = "# Status\nmanual\n<!-- AUTO:BEGIN old -->\nstale\n<!-- AUTO:END -->\ntail\n"
        out = report.replace_auto_block(text, "fresh")
        self.assertIn("manual", out)
        self.assertIn("tail", out)
        self.assertIn("fresh", out)
        self.assertNotIn("stale", out)
        self.assertEqual(out.count(report.AUTO_END), 1)

    def test_auto_block_appended_when_missing(self):
        out = report.replace_auto_block("# Status\n", "fresh")
        self.assertTrue(out.startswith("# Status"))
        self.assertIn("fresh", out)

    def test_missing_and_failed_suites_are_not_hidden(self):
        with tempfile.TemporaryDirectory() as d:
            logs = Path(d)
            (logs / "tests-unit-2026-01-01.json").write_text(json.dumps(
                [{"name": "a", "result": "PASSED"}, {"name": "b", "result": "FAILED"}]))
            (logs / "tests-unit-2026-01-02.json").write_text(json.dumps([{"name": "a", "result": "PASSED"}]))
            (logs / "tests-network-2026-01-01.json").write_text("{broken")
            t = report.latest_test_logs(logs)
            self.assertEqual(t["unit"]["file"], "tests-unit-2026-01-02.json")  # newest log wins
            self.assertEqual((t["unit"]["passed"], t["unit"]["total"]), (1, 1))
            self.assertEqual(t["network"]["error"], "unreadable")
            self.assertNotIn("integration", t)  # no log -> absent, never assumed passing

    def test_build_reports_missing_suites_in_summary(self):
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            for sub in ("evidence/findings", "evidence/experiments", "evidence/logs"):
                (repo / sub).mkdir(parents=True)
            out = report.build(repo)
            self.assertIn("tests: 0/0 passed (missing: unit, integration, network)", out["summary"])
            self.assertIn("no log found", out["test_results"])


if __name__ == "__main__":
    unittest.main()
