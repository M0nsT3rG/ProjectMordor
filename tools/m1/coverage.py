"""ETW coverage experiment for M1 network monitoring (FB-34-01, EXP-046). Never touches the game.

Each case is one real trace (netwatch start/stop/analyze) around the stand-in (tools/m1/standin.py), which performs
one kind of traffic. The expected analysis is fixed here before the runs. Elevated, it shows which mechanisms the
monitor attributes to the target: IPv6, a TCP attempt that never connects, a DNS lookup, very short flows, a child
process, and lost events. Without elevation every case must be not_covered: nothing passes unmonitored.

Not live-testable, and covered elsewhere: PID reuse (unit-tested in test_m1.py) and traffic another process makes on
the target's behalf (a documented limitation, docs/m1-criteria.md section 4a).
Writes evidence/logs/m1-coverage-<time>-<elevated|not-elevated>/result.json and never overwrites one.
  python -I tools/m1/coverage.py [--repo <project root>]
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import netwatch  # noqa: E402
from dryrun import elevated, git_state  # noqa: E402

TCP_CONNECT = netwatch.TCP_CONNECT
# name: (stand-in arguments, tiny buffers, check(analysis, status) -> (verdict, why)). Verdicts: MATCH, MISMATCH,
# NOT_APPLICABLE (the OS refused to send, so there was nothing to see), NOT_DEMONSTRATED (the condition did not occur).


def refused(st):
    return [k for k, v in st.items() if isinstance(v, str) and v.startswith("refused")]


def has(r, proto, ip, peer_public, child=None):
    return any(c["proto"] == proto and c["ip"] == ip and (c["peer"] != "loopback") == peer_public
               and (child is None or c["from_child"] == child) for c in r["contacts"])


def covered(r):  # a MATCH needs proven coverage too: an ambiguous or lost attribution is never a confirmation
    return r.get("coverage") == "covered"


def covered_clean_with_v6(r, st):
    if refused(st):
        return "NOT_APPLICABLE", f"IPv6 loopback refused: {refused(st)}"
    ok = covered(r) and r["verdict"] == "clean" and has(r, "tcp", "v6", False) and has(r, "udp", "v6", False)
    return ("MATCH" if ok else "MISMATCH"), f"verdict {r['verdict']}, IPv6 TCP {has(r, 'tcp', 'v6', False)}, "                                             f"IPv6 UDP {has(r, 'udp', 'v6', False)}"


def contact(proto, ip, never_connects=False):
    """The claimed transport and family must be attributed, not just any unexpected contact."""
    def check(r, st):
        if refused(st):  # nothing left the stand-in, so there was nothing to attribute
            return "NOT_APPLICABLE", f"the OS refused: {refused(st)}"
        if never_connects and any(v == "connected" for v in st.values()):
            return "NOT_APPLICABLE", "the TCP attempt connected, so it is not a never-connecting attempt"
        if never_connects and not any(v == "attempted, never connected" for v in st.values()):
            return "MISMATCH", f"no never-connecting attempt recorded by the stand-in: {st}"
        ok = covered(r) and r["verdict"] == "unexpected_contact" and has(r, proto, ip, True)
        return ("MATCH" if ok else "MISMATCH"), f"verdict {r['verdict']}, public {proto}/{ip} {has(r, proto, ip, True)}"
    return check


def dns(r, st):
    ok = covered(r) and r["verdict"] == "unexpected_contact" and any(d["name"] == "mordor-test.invalid" for d in r["dns"])
    return ("MATCH" if ok else "MISMATCH"), f"verdict {r['verdict']}, dns {[d['name'] for d in r['dns']]}"


def brief(r, st):
    if refused(st):
        return "NOT_APPLICABLE", f"the OS refused: {refused(st)}"
    n = sum(c["count"] for c in r["contacts"] if c["event_id"] == TCP_CONNECT and c["peer"] == "loopback")
    ok = covered(r) and r["verdict"] == "unexpected_contact" and n >= 50 and has(r, "udp", "v4", True)
    return ("MATCH" if ok else "MISMATCH"), f"verdict {r['verdict']}, loopback TCP connects attributed {n} of 50"


def child(r, st):
    if refused(st):
        return "NOT_APPLICABLE", f"the OS refused the child's send: {refused(st)}"
    ok = covered(r) and r["verdict"] == "unexpected_contact" and has(r, "udp", "v4", True, child=True)
    return ("MATCH" if ok else "MISMATCH"), f"verdict {r['verdict']}, child UDP attributed {has(r, 'udp', 'v4', True, True)}"


def loss(r, st):
    if r.get("events_lost") == 0:
        return "NOT_DEMONSTRATED", "no event was lost with the smallest buffers"
    ok = r["verdict"] == "not_covered" and (r.get("events_lost") or 0) > 0
    return ("MATCH" if ok else "MISMATCH"), f"verdict {r['verdict']}, events lost {r.get('events_lost')}"


CASES = {
    "ipv6_loopback_attributed": (["--loop6"], False, covered_clean_with_v6),
    "tcp_attempt_never_connects": (["--tcp", "192.0.2.1"], False, contact("tcp", "v4", never_connects=True)),
    "ipv6_public_udp": (["--udp", "2001:db8::1"], False, contact("udp", "v6")),
    "dns_lookup_only": (["--dns", "mordor-test.invalid"], False, dns),
    "brief_flows_then_contact": (["--brief", "50", "--udp", "192.0.2.1"], False, brief),
    "child_process_contact": (["--child-udp", "192.0.2.1"], False, child),
    "forced_event_loss": (["--flood", "200000"], True, loss),
}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", type=Path, default=HERE.parents[1])
    a = ap.parse_args(argv)
    up = elevated()
    stamp = dt.datetime.now().strftime("%Y-%m-%d-%H%M%S")
    out_dir = a.repo / "evidence" / "logs" / f"m1-coverage-{stamp}-{'elevated' if up else 'not-elevated'}"
    out_dir.mkdir(parents=True, exist_ok=False)  # evidence is immutable
    head, dirty = git_state(a.repo)
    tmp = Path(tempfile.mkdtemp(prefix="mordor-m1-coverage-"))
    results = []
    try:
        for name, (args, tiny, check) in CASES.items():
            work, log, status = tmp / name, tmp / f"{name}.log", tmp / f"{name}.status.json"
            try:
                netwatch.start(work, tiny_buffers=tiny)
                p = subprocess.Popen([sys.executable, "-I", str(HERE / "standin.py"), "--log", str(log), "--mode",
                                      "none", "--status", str(status), *args])
                p.wait(timeout=600)
                netwatch.stop(work)
            except BaseException:  # interrupted or failed: never leave the trace running or its raw files (FB-35-04)
                netwatch.abort(work)
                raise
            r = netwatch.analyze(work, [], [p.pid])
            st = json.loads(status.read_text(encoding="utf-8")) if status.is_file() else {}
            if up:
                verdict, why = check(r, st)
            else:  # unmonitored: nothing may come out as covered or clean
                verdict = "MATCH" if r["verdict"] == "not_covered" else "MISMATCH"
                why = f"verdict {r['verdict']} (expected not_covered without elevation)"
            raw_left = [f.name for f in work.iterdir() if f.name in netwatch.RAW] if work.is_dir() else []
            if raw_left or r["raw_not_deleted"]:
                verdict, why = "MISMATCH", f"raw trace left: {raw_left or r['raw_not_deleted']}"
            results.append({"case": name, "verdict": verdict, "why": why, "stand_in": st,
                            "network": {k: r.get(k) for k in ("verdict", "coverage_problems", "events_lost", "contacts",
                                                              "dns", "targets")}})
            print(f"{name}: {verdict} ({why})")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    ok = all(x["verdict"] in ("MATCH", "NOT_APPLICABLE", "NOT_DEMONSTRATED") for x in results)
    doc = {"experiment": "EXP-046", "elevated": up, "commit": head, "dirty": dirty, "game_touched": False,
           "cases": results, "no_mismatch": ok}
    text = (json.dumps(doc, indent=2) + "\n").replace(str(tmp), "%TEMP%")
    (out_dir / "result.json").write_text(text, encoding="utf-8")
    print(f"{'NO MISMATCH' if ok else 'MISMATCHES'}: {out_dir.relative_to(a.repo).as_posix()}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
