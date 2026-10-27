"""Self-checks for check_gog_build.py (FB-30-04, FB-32-02): python -I tools/environment/test_check_gog_build.py"""
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_gog_build as c  # noqa: E402

FILES = {"ShadowOfWar.exe": b"MZ not a real executable, only a fixture", "steam_api64.dll": b"valve", "bink2w64.dll": b"bink"}
PIN = {"game_id": "42", "build_id": "777", "file_version": "1.0.2445.0", "addons": {"9": "Some Pack"},
       "x64": {n: hashlib.sha256(b).hexdigest().upper() for n, b in FILES.items()}}
GOOD_VERSION = lambda p: {"file_version_fixed": "1.0.2445.0"}  # noqa: E731  (version.dll stand-in)


def install(root: Path, build_id="777", extra=(), addons=("9",)):
    (root / "x64").mkdir()
    for n, b in FILES.items():
        (root / "x64" / n).write_bytes(b)
    for name in extra:
        (root / "x64" / name).write_bytes(b"x")
    (root / "goggame-42.info").write_text(json.dumps({"gameId": "42", "buildId": build_id}), encoding="utf-8")
    for a in addons:
        (root / f"goggame-{a}.info").write_text(json.dumps({"gameId": a, "buildId": build_id, "rootGameId": "42"}),
                                               encoding="utf-8")


class Check(unittest.TestCase):
    def verdict(self, mutate=None, version=GOOD_VERSION, **kw):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            install(root, **kw)
            if mutate:
                mutate(root)
            r = c.check(root, PIN, version)
            return c.verdict(r)[0], r["mismatch"] + r["incomplete"]

    def test_the_pinned_set_is_verified(self):
        self.assertEqual(self.verdict(), ("VERIFIED", []))

    def test_a_changed_exe_or_dll_is_a_mismatch(self):
        for name in ("ShadowOfWar.exe", "bink2w64.dll"):
            v, why = self.verdict(lambda r, n=name: (r / "x64" / n).write_bytes(b"patched"))
            self.assertEqual(v, "MISMATCH", name)
            self.assertTrue(any(name in w for w in why), why)

    def test_an_extra_or_missing_x64_file_is_a_mismatch(self):
        self.assertEqual(self.verdict(extra=("steam_emu.ini",))[0], "MISMATCH")  # a crack or emulator file
        self.assertEqual(self.verdict(extra=("dinput8.dll",))[0], "MISMATCH")  # a proxy DLL
        self.assertEqual(self.verdict(lambda r: (r / "x64" / "bink2w64.dll").unlink())[0], "MISMATCH")

    def test_store_metadata(self):
        self.assertEqual(self.verdict(build_id="778")[0], "MISMATCH")
        self.assertEqual(self.verdict(addons=())[0], "MISMATCH")  # a pinned add-on is missing
        v, why = self.verdict(lambda r: (r / "goggame-42.info").write_text("{not json", encoding="utf-8"))
        self.assertEqual(v, "INCOMPLETE")  # FB-32-02: malformed is not a pass, and not silently ignored
        self.assertTrue(any("malformed" in w for w in why))
        self.assertEqual(self.verdict(lambda r: (r / "goggame-9.info").write_text("[1, 2]", encoding="utf-8"))[0],
                         "INCOMPLETE")
        huge = '{"unused": ' + "9" * 5000 + "}"  # over Python's int-digit limit: ValueError, not JSONDecodeError
        self.assertEqual(self.verdict(lambda r: (r / "goggame-42.info").write_text(huge, encoding="utf-8"))[0],
                         "INCOMPLETE")
        wrong_id = json.dumps({"gameId": "10", "buildId": "777", "rootGameId": "42"})  # Codex: identity unchecked
        self.assertEqual(self.verdict(lambda r: (r / "goggame-9.info").write_text(wrong_id, encoding="utf-8"))[0],
                         "MISMATCH")

    def test_names_that_differ_only_in_case_are_a_mismatch(self):  # a case-sensitive x64 directory
        with tempfile.TemporaryDirectory() as tmp:
            root, alt = Path(tmp) / "game", Path(tmp) / "alt"
            root.mkdir()
            alt.mkdir()
            install(root)
            (alt / "BINK2W64.DLL").write_bytes(b"patched")
            listing = sorted((root / "x64").iterdir()) + [alt / "BINK2W64.DLL"]  # the duplicate enumerated last
            real = Path.iterdir
            Path.iterdir = lambda self: iter(listing) if self == root / "x64" else real(self)
            try:
                self.assertEqual(c.verdict(c.check(root, PIN, GOOD_VERSION))[0], "MISMATCH")
            finally:
                Path.iterdir = real

    def test_an_unreadable_file_version_is_incomplete_never_verified(self):  # FB-32-02
        v, why = self.verdict(version=lambda p: {"present": False})
        self.assertEqual(v, "INCOMPLETE")
        self.assertTrue(any("FileVersion" in w for w in why))
        self.assertEqual(self.verdict(version=lambda p: {"file_version_fixed": "1.0.9999.0"})[0], "MISMATCH")

    def test_unexpected_layouts(self):
        self.assertEqual(self.verdict(lambda r: [p.unlink() for p in (r / "x64").iterdir()] and (r / "x64").rmdir())[0],
                         "MISMATCH")  # no x64 at all
        def exe_is_a_directory(r):
            (r / "x64" / "ShadowOfWar.exe").unlink()
            (r / "x64" / "ShadowOfWar.exe").mkdir()
        self.assertEqual(self.verdict(exe_is_a_directory)[0], "MISMATCH")

    def test_an_unreadable_pinned_file_is_incomplete(self):
        real = c.sha256
        def failing(p):
            if p.name == "bink2w64.dll":
                raise PermissionError("locked")
            return real(p)
        c.sha256 = failing
        try:
            v, why = self.verdict()
        finally:
            c.sha256 = real
        self.assertEqual(v, "INCOMPLETE")
        self.assertTrue(any("bink2w64.dll unreadable" in w for w in why), why)

    def test_main_exit_codes(self):
        self.assertEqual(c.verdict({"mismatch": [], "incomplete": ["x"]}), ("INCOMPLETE", 5))
        self.assertEqual(c.verdict({"mismatch": ["y"], "incomplete": ["x"]}), ("MISMATCH", 3))  # a mismatch wins
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(c.main(["--path", str(Path(tmp) / "absent"), "--no-write"]), 4)
            install(Path(tmp))
            self.assertEqual(c.main(["--path", tmp, "--no-write"]), 3)  # the real PIN never matches a fixture


if __name__ == "__main__":
    unittest.main()
