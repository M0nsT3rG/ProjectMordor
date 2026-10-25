"""Bounded probe runs for the KI-35 runners (Codex, PR #25): stdout goes straight to its evidence file, so a hung
probe keeps the trials it finished; a hung probe is dumped (scripts/dump-stacks.ps1, local %TEMP% only) and killed."""
from __future__ import annotations

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def dump_stacks(pid: int, timeout_s: float) -> None:
    # shortcut: the timeout kills PowerShell, not a cdb it started; a stuck cdb outlives the batch (rare, by hand).
    try:
        subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                        str(ROOT / "scripts" / "dump-stacks.ps1"), "-ProcessId", str(pid)],
                       capture_output=True, timeout=timeout_s)
    except subprocess.TimeoutExpired:
        pass


def run_bounded(cmd: list[str], stdout_path: Path, timeout_s: float, dump_timeout_s: float = 120,
                dump=dump_stacks) -> int | str:
    """The exit code, or "hung" when the probe outlived timeout_s (dumped, then killed)."""
    with open(stdout_path, "w", encoding="utf-8") as out:
        p = subprocess.Popen(cmd, stdout=out, stderr=subprocess.DEVNULL)
        try:
            return p.wait(timeout=timeout_s)
        except subprocess.TimeoutExpired:
            dump(p.pid, dump_timeout_s)
            p.kill()
            p.wait()
            return "hung"
