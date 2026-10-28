"""ETW coverage experiment for M1 network monitoring (FB-34-01; EXP-046, re-run with the session buffers as EXP-050).
Never touches the game.

Each case is one real trace (netwatch start/stop/analyze) around the stand-in (tools/m1/standin.py), which performs
one kind of traffic. The expected analysis is fixed here before the runs. Elevated, it shows which mechanisms the
monitor attributes to the target: IPv6, a TCP attempt that never connects, a DNS lookup, very short flows, a child
process, and lost events; and that the session buffers (netwatch.BUFFERS, KI-45) lose nothing under the flood that
the tiny buffers lost events to, nor under every kind of traffic at once beside an unrelated flooding process (review
#39, FB-39-03). Without elevation every case must be not_covered: nothing passes unmonitored.

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
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import netwatch  # noqa: E402
from dryrun import elevated, git_state  # noqa: E402

TCP_CONNECT = netwatch.TCP_CONNECT
FLOOD = 200000  # loopback datagrams: the tiny buffers lost events to it in EXP-046
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


def no_loss(r, st):
    """EXP-050 (KI-45): the session buffers must lose nothing under the flood that the tiny buffers lost events to."""
    n = sum(c["count"] for c in r["contacts"] if c["event_id"] == netwatch.UDP_SEND and c["peer"] == "loopback")
    done = st.get("flood") == FLOOD  # the stand-in writes it only after the last send (Codex pass 9)
    ok = covered(r) and r.get("events_lost") == 0 and n > 0 and done
    return ("MATCH" if ok else "MISMATCH"), (f"verdict {r['verdict']}, flood completed {done}, events lost "
                                             f"{r.get('events_lost')} of {r.get('events_total')}, "
                                             f"loopback UDP sends attributed {n}")


def mixed(r, st):
    """EXP-050, review #39 (FB-39-03): with the session buffers, every kind of traffic from the target and its child,
    one after the other, while unrelated processes flood loopback for the target's whole life, must lose nothing and
    still be attributed. A stand-in that stopped early (a flood send the OS refused ends it) is a MISMATCH."""
    if refused(st):
        return "NOT_APPLICABLE", f"the OS refused: {refused(st)}"
    n = sum(c["count"] for c in r["contacts"] if c["event_id"] == TCP_CONNECT and c["peer"] == "loopback")
    flood = sum(c["count"] for c in r["contacts"] if c["event_id"] == netwatch.UDP_SEND and c["peer"] == "loopback"
                and c["ip"] == "v4")  # Codex pass 13: completed is not attributed
    seen = {"ipv6 tcp": has(r, "tcp", "v6", False), "ipv6 udp": has(r, "udp", "v6", False),
            "public udp": has(r, "udp", "v4", True, child=False), "child udp": has(r, "udp", "v4", True, child=True),
            "dns": any(d["name"] == "mordor-test.invalid" for d in r["dns"]), "brief tcp": n >= 50,
            "flood attributed": flood > 0, "flood completed": st.get("flood") == FLOOD,
            "background complete": st.get("background complete") is True}
    ok = covered(r) and r.get("events_lost") == 0 and r["verdict"] == "unexpected_contact" and all(seen.values())
    return ("MATCH" if ok else "MISMATCH"), (f"verdict {r['verdict']}, events lost {r.get('events_lost')} of "
                                             f"{r.get('events_total')}, missing {[k for k, v in seen.items() if not v]}")


# Unrelated background traffic: stand-ins that are not targets, one after the other for the case stand-in's whole life
# (a finished one is replaced within BG_POLL_S). Flood only: a repeated status key would hide a refusal (Codex pass 13).
BACKGROUND = {"mixed_load_with_session_buffers": ["--flood", str(2 * FLOOD)]}
BG_POLL_S = 0.05

CASES = {
    "ipv6_loopback_attributed": (["--loop6"], False, covered_clean_with_v6),
    "tcp_attempt_never_connects": (["--tcp", "192.0.2.1"], False, contact("tcp", "v4", never_connects=True)),
    "ipv6_public_udp": (["--udp", "2001:db8::1"], False, contact("udp", "v6")),
    "dns_lookup_only": (["--dns", "mordor-test.invalid"], False, dns),
    "brief_flows_then_contact": (["--brief", "50", "--udp", "192.0.2.1"], False, brief),
    "child_process_contact": (["--child-udp", "192.0.2.1"], False, child),
    "forced_event_loss": (["--flood", str(FLOOD)], True, loss),
    "flood_with_session_buffers": (["--flood", str(FLOOD)], False, no_loss),
    "mixed_load_with_session_buffers": (["--loop6", "--brief", "50", "--flood", str(FLOOD), "--dns",
                                         "mordor-test.invalid", "--udp", "192.0.2.1", "--child-udp", "192.0.2.1"],
                                        False, mixed),
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
            bgs, p = [], None

            def background():
                bgs.append(subprocess.Popen([sys.executable, "-I", str(HERE / "standin.py"), "--log", str(log), "--mode",
                                             "none", "--status", str(tmp / f"{name}.bg{len(bgs)}.json"),
                                             *BACKGROUND[name]]))
            try:
                netwatch.start(work, tiny_buffers=tiny)
                if name in BACKGROUND:
                    background()
                p = subprocess.Popen([sys.executable, "-I", str(HERE / "standin.py"), "--log", str(log), "--mode",
                                      "none", "--status", str(status), *args])
                deadline = time.monotonic() + 600
                while p.poll() is None:  # the background never stops while the target runs
                    if bgs and bgs[-1].poll() is not None:
                        background()
                    if time.monotonic() > deadline:
                        raise TimeoutError(f"{name}: the stand-in did not finish")
                    time.sleep(BG_POLL_S)
                for b in bgs:
                    b.wait(timeout=600)
                netwatch.stop(work)
            except BaseException:  # interrupted or failed: never leave the trace running or its raw files (FB-35-04)
                for x in bgs + [p]:
                    if x and x.poll() is None:
                        x.kill()
                netwatch.abort(work)
                raise
            r = netwatch.analyze(work, [], [p.pid])
            st = json.loads(status.read_text(encoding="utf-8")) if status.is_file() else {}
            if bgs:  # every background stand-in must have sent its whole flood: a stopped one was no load
                done = [json.loads(f.read_text(encoding="utf-8")).get("flood") if f.is_file() else None
                        for f in (tmp / f"{name}.bg{i}.json" for i in range(len(bgs)))]
                st.update({"background runs": len(bgs), "background complete": all(d == 2 * FLOOD for d in done)})
            if up:
                verdict, why = check(r, st)
            else:  # unmonitored: nothing may come out as covered or clean
                verdict = "MATCH" if r["verdict"] == "not_covered" else "MISMATCH"
                why = f"verdict {r['verdict']} (expected not_covered without elevation)"
            raw_left = [f.name for f in work.iterdir() if f.name in netwatch.RAW] if work.is_dir() else []
            if raw_left or r["raw_not_deleted"]:
                verdict, why = "MISMATCH", f"raw trace left: {raw_left or r['raw_not_deleted']}"
            results.append({"case": name, "verdict": verdict, "why": why, "stand_in": st,
                            "network": {k: r.get(k) for k in ("verdict", "coverage_problems", "events_lost", "events_total", "buffers", "contacts",
                                                              "dns", "targets")}})
            print(f"{name}: {verdict} ({why})")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    ok = all(x["verdict"] in ("MATCH", "NOT_APPLICABLE", "NOT_DEMONSTRATED") for x in results)
    doc = {"experiment": "EXP-050", "elevated": up, "buffers": netwatch.BUFFERS, "commit": head, "dirty": dirty, "game_touched": False,
           "cases": results, "no_mismatch": ok}
    text = (json.dumps(doc, indent=2) + "\n").replace(str(tmp), "%TEMP%")
    (out_dir / "result.json").write_text(text, encoding="utf-8")
    print(f"{'NO MISMATCH' if ok else 'MISMATCHES'}: {out_dir.relative_to(a.repo).as_posix()}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
