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

    def test_suite_names_match_exactly_and_skips_are_not_passes(self):
        with tempfile.TemporaryDirectory() as d:
            logs = Path(d)
            (logs / "tests-integration-2026-01-01.json").write_text(json.dumps(
                [{"name": "a", "result": "PASSED"}, {"name": "b", "result": "SKIPPED"}, {"name": "c", "result": "FAILED"}]))
            # Lexicographically later, but other suites/runs: must never be picked for 'integration'.
            (logs / "tests-integration-gns-2026-01-02.json").write_text(json.dumps([{"name": "x", "result": "PASSED"}]))
            (logs / "tests-integration-gns-first-run-2026-01-03.json").write_text(json.dumps([{"name": "y", "result": "FAILED"}]))
            t = report.latest_test_logs(logs)["integration"]
            self.assertEqual(t["file"], "tests-integration-2026-01-01.json")
            self.assertEqual((t["passed"], t["skipped"], t["failed"]), (1, ["b"], ["c"]))
            g = report.latest_test_logs(logs, ("integration-gns",))["integration-gns"]
            self.assertEqual(g["file"], "tests-integration-gns-2026-01-02.json")

    def test_per_run_directory_beats_legacy_dated_files(self):
        # FB-17-02: test.ps1 now writes tests-<time>-<commit>[-dirty]/<suite>.json; the newest timestamp wins.
        with tempfile.TemporaryDirectory() as d:
            logs = Path(d)
            (logs / "tests-unit-2026-01-02.json").write_text(json.dumps([{"name": "a", "result": "FAILED"}]))
            for run, res in (("tests-2026-01-02-090000-abcdef1", "FAILED"), ("tests-2026-01-02-100000-abcdef1-dirty", "PASSED")):
                (logs / run).mkdir()
                (logs / run / "unit.json").write_text(json.dumps([{"name": "a", "result": res}]))
            (logs / "tests-2026-01-03-000000-notacommit").mkdir()  # not a run directory name: ignored
            (logs / "tests-2026-01-03-000000-notacommit" / "unit.json").write_text("[]")
            t = report.latest_test_logs(logs)["unit"]
            self.assertEqual(t["file"], "tests-2026-01-02-100000-abcdef1-dirty/unit.json")
            self.assertEqual((t["passed"], t["failed"]), (1, []))
            self.assertNotIn("integration", report.latest_test_logs(logs))  # a run dir without it adds nothing

    def test_e2e_logs_inside_run_directories_are_listed(self):
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            for sub in ("evidence/findings", "evidence/experiments", "evidence/logs/e2e-2026-01-01",
                        "evidence/logs/tests-2026-01-02-100000-abcdef1/e2e"):
                (repo / sub).mkdir(parents=True)
            text = "\n".join(report.build(repo).values())
            self.assertIn("e2e-2026-01-01", text)
            self.assertIn("tests-2026-01-02-100000-abcdef1/e2e", text)

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
