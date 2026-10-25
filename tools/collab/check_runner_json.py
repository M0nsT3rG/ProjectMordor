"""FB-22-01 end-to-end check: the C++ test runner writes valid JSON whatever its --suite holds.

Runs one fast test of a built suite binary with a --suite full of JSON-hostile characters (quote, backslash, tab),
parses the results file with Python's strict JSON reader, and checks the suite round-trips exactly.
Usage: python -I tools/collab/check_runner_json.py build/Release/mordor_unit_tests.exe
Exit 1 on any failure, 2 on usage errors. Run by scripts/test.ps1.
"""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

# ASCII only: on Windows the runner's narrow argv uses the ANSI code page, so a non-ASCII argument would arrive as a
# non-UTF-8 byte; UTF-8 pass-through is covered by the json_escape unit test instead.
HOSTILE = 'we"ird\\suite\tname'


def main(argv: list[str]) -> int:
    if len(argv) != 1 or not Path(argv[0]).is_file():
        print(__doc__)
        return 2
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "results.json"
        exe = str(Path(argv[0]).resolve())  # CreateProcess does not resolve a relative path with forward slashes
        r = subprocess.run([exe, "transform_snapshot_roundtrip", "--json", str(out), "--suite", HOSTILE],
                           capture_output=True, timeout=120)
        if r.returncode != 0 or not out.is_file():
            print(f"runner JSON check: the runner failed (exit {r.returncode})")
            return 1
        try:
            cases = json.loads(out.read_text(encoding="utf-8"))
        except ValueError as e:
            print(f"runner JSON check: INVALID JSON ({e})")
            return 1
    ok = len(cases) == 1 and cases[0].get("suite") == HOSTILE and cases[0].get("result") == "PASSED"
    print("runner JSON check: OK" if ok else f"runner JSON check: FAILED, got {cases!r}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
