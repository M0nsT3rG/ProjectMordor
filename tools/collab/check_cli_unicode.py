"""Codex review of the combined diff (P2): non-ASCII paths on the command line reach our executables intact.

Without the UTF-8 code-page manifest (cmake/utf8-codepage.manifest), a narrow argv carries the system ANSI code
page, and secure_identity.cpp (which reads paths as UTF-8) cannot create or read a key under, say, C:\\Users\\José.
This creates and reads a key under a temporary directory named with non-ASCII letters, through mordor-keygen.
Usage: python -I tools/collab/check_cli_unicode.py build-gns/Release/mordor-keygen.exe
Exit 1 on failure, 2 on usage errors. Run by scripts/test.ps1 -GnsBuild. The key is deleted afterwards.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def main(argv: list[str]) -> int:
    if len(argv) != 1 or not Path(argv[0]).is_file():
        print(__doc__)
        return 2
    exe = str(Path(argv[0]).resolve())
    with tempfile.TemporaryDirectory() as d:
        key = Path(d) / "chave-a\u00e7\u00e3o \u00c1rvore" / "host.key"  # non-ASCII and a space
        key.parent.mkdir()
        new = subprocess.run([exe, "--new", str(key)], capture_output=True, timeout=60)
        show = subprocess.run([exe, "--show", str(key)], capture_output=True, timeout=60) if key.is_file() else None
        ok = new.returncode == 0 and key.is_file() and show is not None and show.returncode == 0
    print("cli unicode check: OK" if ok else
          f"cli unicode check: FAILED (new exit {new.returncode}, key created {key.is_file() if not ok else True}, "
          f"show exit {None if show is None else show.returncode})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
