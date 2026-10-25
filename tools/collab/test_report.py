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

    def test_run_status_is_shown_with_each_suite(self):
        # FB-18-02: a passing suite from a failed or incomplete run must not read as a clean pass.
        with tempfile.TemporaryDirectory() as d:
            logs = Path(d)
            run = logs / "tests-2026-01-02-100000-abcdef1"
            run.mkdir()
            (run / "unit.json").write_text(json.dumps([{"name": "a", "result": "PASSED"}]))
            (run / "run.json").write_text(json.dumps({"result": "failed", "commit": "abcdef1234567", "dirty": True}))
            (logs / "tests-network-2026-01-01.json").write_text(json.dumps([{"name": "n", "result": "PASSED"}]))
            t = report.latest_test_logs(logs)
            self.assertEqual(t["unit"]["run"], {"result": "failed", "commit": "abcdef1", "dirty": True})
            self.assertEqual(report.run_note(t["unit"]), " [**run failed, abcdef1, dirty**]")
            self.assertIsNone(t["network"]["run"])
            self.assertIn("legacy", report.run_note(t["network"]))
            (run / "run.json").write_text("{broken")
            self.assertIn("unknown", report.latest_test_logs(logs)["unit"]["run"]["result"])

    def test_recent_runs_keep_every_invocation_visible(self):
        # FB-19-01: a later pass inside a run never hides an earlier failed invocation; the list is bounded.
        with tempfile.TemporaryDirectory() as d:
            logs = Path(d)
            def mk(name, run):
                (logs / name).mkdir()
                (logs / name / "run.json").write_text(json.dumps(run))
            for i in range(6):
                mk(f"tests-2026-01-0{i + 1}-000000-abcdef1", {"result": "passed", "ctest_runs": [{"dir": "iter-1", "exit_code": 0}]})
            mk("tests-2026-01-09-000000-abcdef1", {"result": "failed", "ctest_runs": [
                {"dir": "iter-1", "exit_code": 1, "missing": []}, {"dir": "iter-2", "exit_code": 0, "missing": ["transport"]}]})
            mk("tests-2026-01-08-000000-abcdef1", {"result": "passed"})  # before per-invocation records
            (logs / "tests-2026-01-07-000000-notacommit").mkdir()  # not a run directory name: ignored
            lines = report.recent_runs(logs)
            self.assertEqual(len(lines), report.RECENT_RUNS)
            self.assertIn("tests-2026-01-09-000000-abcdef1", lines[0])  # newest first
            self.assertIn("**failed**", lines[0])
            self.assertIn("iter-1: exit 1", lines[0])
            self.assertIn("iter-2: exit 0, MISSING transport", lines[0])
            self.assertIn("before per-invocation records", lines[1])
            self.assertFalse(any("notacommit" in x for x in lines))

    def test_unreadable_or_null_run_records_never_crash(self):
        with tempfile.TemporaryDirectory() as d:
            logs = Path(d)
            for name, raw in (("tests-2026-01-01-000000-abcdef1", b"{broken"), ("tests-2026-01-02-000000-abcdef1", b"\xff\xfe\xfa"),
                              ("tests-2026-01-03-000000-abcdef1", b'{"result": "passed", "ctest_runs": null}')):
                (logs / name).mkdir()
                (logs / name / "run.json").write_bytes(raw)
                (logs / name / "unit.json").write_text(json.dumps([{"name": "a", "result": "PASSED"}]))
            lines = report.recent_runs(logs)
            self.assertIn("no ctest invocation recorded", lines[0])
            self.assertIn("no readable run.json", lines[1])  # invalid UTF-8 (a ValueError, not a JSONDecodeError)
            self.assertIn("no readable run.json", lines[2])
            self.assertIn("passed", report.latest_test_logs(logs)["unit"]["run"]["result"])
            (logs / "tests-2026-01-03-000000-abcdef1" / "run.json").write_bytes(b"\xff\xfe\xfa")
            self.assertIn("unknown", report.latest_test_logs(logs)["unit"]["run"]["result"])

    def test_recent_runs_appear_in_both_generated_blocks(self):
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            for sub in ("evidence/findings", "evidence/experiments", "evidence/logs/tests-2026-01-02-100000-abcdef1"):
                (repo / sub).mkdir(parents=True)
            (repo / "evidence/logs/tests-2026-01-02-100000-abcdef1/run.json").write_text(json.dumps(
                {"result": "failed", "ctest_runs": [{"dir": "iter-1", "exit_code": 1}, {"dir": "iter-2", "exit_code": 0}]}))
            out = report.build(repo)
            for block in ("status_block", "test_results"):
                self.assertIn("iter-1: exit 1, iter-2: exit 0", out[block], block)

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
