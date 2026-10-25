"""Self-checks for the evidence validator's path rules: python -I tools/evidence/test_evidence.py"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import evidence as ev  # noqa: E402


def exp(paths):
    r = {k: "x" for k in ev.EXP_KEYS}
    r.update({"id": "EXP-999", "type": "experiment", "date": "2026-10-25", "confidence": "high", "result": "PASSED",
              "related": [], "evidence_paths": paths, "_path": "evidence/experiments/EXP-999.json"})
    return {"EXP-999": r}


class PathRules(unittest.TestCase):
    def test_date_named_test_results_are_rejected(self):
        # FB-17-02: these names are rewritten by every later run on the same day.
        for p in ("evidence/logs/tests-unit-2026-10-25.json", "evidence/logs/tests-integration-gns-2026-10-24.json",
                  "evidence/logs/e2e-2026-10-25", "evidence/logs/e2e-2026-10-25/",
                  "evidence/logs/e2e-2026-10-23/host.log"):  # a file inside the rewritten dir too (Codex review)
            errs = ev.validate(exp([p]))
            self.assertTrue(any("date-named" in e for e in errs), (p, errs))

    def test_per_run_and_recovered_paths_are_not_flagged(self):
        for p in ("evidence/logs/tests-2026-10-25-140000-abcdef1", "evidence/logs/recovered/EXP-027",
                  "evidence/logs/tests-2026-10-25-140000-abcdef1/e2e/host.log", "evidence/logs/secure-e2e-2026-10-25-131209-udp",
                  "evidence/logs/tests-integration-gns-first-run-2026-10-23.json"):
            self.assertFalse(any("date-named" in e for e in ev.validate(exp([p]))), p)

    def test_evidence_is_never_git_ignored(self):
        # An unanchored build-*/ in .gitignore once hid every evidence/logs/tests-*/build-gns-iter-1/ (Codex review).
        import subprocess
        probes = ["evidence/logs/tests-2026-01-01-000000-abcdef1/build-gns-iter-1/dtls.json",
                  "evidence/logs/tests-2026-01-01-000000-abcdef1/iter-1/unit.json",
                  "evidence/logs/soak-2026-01-01-000000-harsh-gns-sec-g6/host.log",
                  "evidence/logs/recovered/EXP-001/abcdef1/tests-unit-2026-01-01.json"]
        r = subprocess.run(["git", "check-ignore", "--no-index", *probes], cwd=ev.ROOT, capture_output=True, text=True)
        self.assertEqual(r.stdout.strip(), "", f"git ignores evidence paths: {r.stdout}")
        self.assertEqual(subprocess.run(["git", "check-ignore", "-q", "--no-index", "build-gns/Release/x.json"], cwd=ev.ROOT).returncode, 0)

    def test_restored_evidence_matches_its_digests(self):
        # FB-22-02 / FB-23-01: restored per-invocation files must still be exactly what both manifests recorded.
        import hashlib, json
        for name in ("manifest.json", "manifest-v2.json"):
            m = json.loads((ev.ROOT / "evidence/logs/restored" / name).read_text(encoding="utf-8"))
            files = [f for r in m["runs"] for i in r["invocations"] for f in i["files"] if "sha256" in f]
            self.assertTrue(files, name)
            for f in files:
                data = (ev.ROOT / f["path"]).read_bytes()
                self.assertEqual((len(data), hashlib.sha256(data).hexdigest()), (f["bytes"], f["sha256"]), f"{name}: {f['path']}")

    def test_manifest_v2_states_completeness_per_file(self):
        # FB-23-01: every recorded invocation is complete file by file; only build-gns-iter-* files were restored.
        import json
        m = json.loads((ev.ROOT / "evidence/logs/restored/manifest-v2.json").read_text(encoding="utf-8"))
        invs = [i for r in m["runs"] for i in r["invocations"]]
        self.assertTrue(invs)
        self.assertEqual({i["status"] for i in invs}, {"complete"})
        restored = [f["path"] for i in invs for f in i["files"] if f.get("restored")]
        self.assertTrue(restored and all("/build-gns-iter-" in p for p in restored))

    def test_repository_records_pass(self):
        recs, errs = ev.load_records()
        self.assertEqual(errs + ev.validate(recs), [])


if __name__ == "__main__":
    unittest.main()
