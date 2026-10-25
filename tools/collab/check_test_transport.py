"""FB-13-03: fail when a recorded test suite ran on a transport other than the one it is registered for.

A MORDOR_TRANSPORT left in a caller's environment once made the plain (UDP) ctest suites run on GNS (EXP-025).
CMake now pins it per suite; this check proves it from the results themselves. Each argument is SUITE=EXPECTED
for a results file RUN_DIR/SUITE.json, the per-run directory scripts/test.ps1 writes (or a path: SUITE=EXPECTED=PATH).
Usage: python -I tools/collab/check_test_transport.py --dir evidence/logs/tests-<time>-<commit> integration=udp integration-gns=gns ...
Exit 1 on any mismatch, 2 on usage errors.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path


def mismatches(results: list[dict], expected: str) -> list[str]:
    return sorted({r.get("name", "?") for r in results if r.get("transport") != expected})


def main(argv: list[str]) -> int:
    if len(argv) < 3 or argv[0] != "--dir":
        print(__doc__)
        return 2
    run_dir, bad = Path(argv[1]), 0
    for spec in argv[2:]:
        parts = spec.split("=")
        if len(parts) not in (2, 3):
            print(f"bad argument: {spec}")
            return 2
        suite, expected = parts[0], parts[1]
        path = Path(parts[2]) if len(parts) == 3 else run_dir / f"{suite}.json"
        if not path.is_file():
            continue  # a suite that did not run this time is reported by ctest, not here
        wrong = mismatches(json.loads(path.read_text(encoding="utf-8")), expected)
        if wrong:
            bad += 1
            print(f"TRANSPORT MISMATCH {suite}: expected {expected}, got another transport in {len(wrong)} tests, e.g. {wrong[:3]}")
    print("transport check: OK" if not bad else f"transport check: {bad} suite(s) ran on the wrong transport")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
