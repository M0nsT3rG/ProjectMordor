"""Self-checks for check_gog_build.py (FB-30-04): python -I tools/environment/test_check_gog_build.py"""
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_gog_build as c  # noqa: E402

EXE = b"MZ not a real executable, only a fixture"


def install(root: Path, build_id="777", extra=(), addons=("9",)):
    (root / "x64").mkdir()
    (root / "x64" / "ShadowOfWar.exe").write_bytes(EXE)
    (root / "x64" / "steam_api64.dll").write_bytes(b"valve")  # shipped by the real GOG release
    for name in extra:
        (root / "x64" / name).write_bytes(b"x")
    (root / "goggame-42.info").write_text(json.dumps({"gameId": "42", "buildId": build_id}), encoding="utf-8")
    for a in addons:
        (root / f"goggame-{a}.info").write_text(json.dumps({"gameId": a, "buildId": build_id, "rootGameId": "42"}),
                                               encoding="utf-8")


PIN = {"game_id": "42", "build_id": "777", "file_version": "1.0.2445.0", "sha256": hashlib.sha256(EXE).hexdigest().upper(),
       "addons": {"9": "Some Pack"}}


class Check(unittest.TestCase):
    def run_check(self, **kw):
        with tempfile.TemporaryDirectory() as tmp:
            install(Path(tmp), **kw)
            return c.check(Path(tmp), PIN)["problems"]

    def test_the_pinned_build_passes(self):
        self.assertEqual(self.run_check(), [])

    def test_a_changed_executable_is_a_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            install(Path(tmp))
            (Path(tmp) / "x64" / "ShadowOfWar.exe").write_bytes(EXE + b"patched")
            self.assertTrue(any("SHA-256" in p for p in c.check(Path(tmp), PIN)["problems"]))

    def test_a_changed_store_build_is_a_mismatch(self):
        self.assertTrue(any("buildId" in p for p in self.run_check(build_id="778")))

    def test_crack_files_and_proxy_dlls_are_reported(self):
        problems = self.run_check(extra=("steam_emu.ini", "codex64.dll", "dinput8.dll"))
        self.assertTrue(any("not part of the release" in p for p in problems))
        self.assertTrue(any("proxy DLLs" in p for p in problems))

    def test_a_missing_pinned_add_on_is_a_mismatch(self):  # the amendment pins the installed set
        self.assertTrue(any("add-on missing" in p for p in self.run_check(addons=())))

    def test_missing_store_metadata_is_a_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            install(Path(tmp))
            (Path(tmp) / "goggame-42.info").unlink()
            self.assertTrue(any("store metadata" in p for p in c.check(Path(tmp), PIN)["problems"]))

    def test_main_exit_codes(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(c.main(["--path", str(Path(tmp) / "absent"), "--no-write"]), 4)
            install(Path(tmp))
            self.assertEqual(c.main(["--path", tmp, "--no-write"]), 3)  # the real PIN never matches a fixture


if __name__ == "__main__":
    unittest.main()
