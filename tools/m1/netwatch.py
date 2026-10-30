"""Per-process network monitoring for M1 sessions (FB-33-02), with Windows' own event tracing only (logman, tracerpt).

One ETW session records, for every process, each TCP/UDP event (Microsoft-Windows-Kernel-Network), each DNS lookup
(Microsoft-Windows-DNS-Client) and each process start and exit (Microsoft-Windows-Kernel-Process). Event tracing is
not sampling, so brief connections are recorded too. Starting and stopping the trace needs an elevated prompt; nothing
is installed or configured, and the trace session exists only between `start` and `stop`.

Coverage is proven, never assumed: a canary process makes one loopback TCP connect, one loopback UDP send and one DNS
lookup of a reserved name right after the trace starts and right before it stops. Monitoring counts as covering the
window only if both canaries are fully seen, no event was lost, and every target process has both its start and its
exit inside the trace. Anything less is `not_covered`, which never passes (M1 stop condition 9).

The raw trace holds every process's traffic on the machine, so it stays in the work directory (under %TEMP%) and
`analyze` deletes it unless --keep-raw. The reduced result holds no addresses: only loopback/private/public, ports and
DNS names asked by the target processes.

  python -I tools/m1/netwatch.py start   --work DIR          (elevated)
  python -I tools/m1/netwatch.py stop    --work DIR          (elevated)
  python -I tools/m1/netwatch.py analyze --work DIR --out FILE (--image NAME | --pid N)...
  python -I tools/m1/netwatch.py canary  TAG                  (run by start and stop)
  python -I tools/m1/netwatch.py cleanup --work DIR          (after an interrupted run: delete the raw trace)
"""
from __future__ import annotations

import argparse
import ipaddress
import json
import os
import re
import socket
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

SESSION = "mordor-m1-net"
PROVIDERS = ['"Microsoft-Windows-Kernel-Network" 0x30 0x4',  # IPv4 and IPv6 keywords
             '"Microsoft-Windows-DNS-Client" 0xffffffffffffffff 0x5',
             '"Microsoft-Windows-Kernel-Process" 0x10 0x4']  # WINEVENT_KEYWORD_PROCESS
NET, DNS, PROC = "Microsoft-Windows-Kernel-Network", "Microsoft-Windows-DNS-Client", "Microsoft-Windows-Kernel-Process"
# Event ids as decoded on this machine (format probe, 2026-10-27): network 12 TCP connect, 42 UDP send;
# DNS 3006 query started; process 1 start, 2 exit.
TCP_CONNECT, UDP_SEND, DNS_QUERY, PROC_START, PROC_STOP = "12", "42", "3006", "1", "2"
NS = {"e": "http://schemas.microsoft.com/win/2004/08/events/event"}
RAW = ("trace.etl", "trace.xml", "summary.txt", "providers.txt")
# Session buffers (KI-45): logman's defaults lost 1203 events in EXP-048 session 1. 256 KB x 64..512 buffers: at most
# 128 MB, measured under a loopback flood by EXP-050 (coverage.py) before any session uses them.
BUFFERS = ["-bs", "256", "-nb", "64", "512"]
TINY_BUFFERS = ["-bs", "1", "-nb", "2", "2"]


def canary_name(tag: str) -> str:
    return f"mordor-canary-{tag}.invalid"  # RFC 6761: never resolvable


def canary(tag: str) -> int:
    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    c = socket.create_connection(srv.getsockname(), timeout=2)
    a, _ = srv.accept()
    c.sendall(b"canary")
    a.recv(16)
    for s in (c, a, srv):
        s.close()
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.sendto(b"canary", ("127.0.0.1", 9))
    u.close()
    try:
        socket.getaddrinfo(canary_name(tag), 80)
    except OSError:
        pass  # the lookup itself is what is traced
    return os.getpid()


def run_canary(tag: str) -> int | None:
    try:
        p = subprocess.run([sys.executable, "-I", __file__, "canary", tag], capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:  # a missing canary is lost coverage; it must not skip stopping the trace
        return None
    return int(p.stdout.strip()) if p.returncode == 0 and p.stdout.strip().isdigit() else None


def cleanup(work: Path) -> list[str]:
    """Delete the raw trace files; return the ones that could not be deleted (FB-34-04)."""
    left = []
    for name in RAW:
        try:
            (work / name).unlink(missing_ok=True)
        except OSError:  # one locked file must not keep the others
            left.append(name)
    return left


def start(work: Path, tiny_buffers: bool = False) -> int:
    """tiny_buffers: coverage experiment only (EXP-046, EXP-050), to provoke and observe lost events."""
    work.mkdir(parents=True, exist_ok=True)
    subprocess.run(["logman", "stop", SESSION, "-ets"], capture_output=True)  # a session left by an interrupted run
    left = cleanup(work)  # raw files left by an interrupted run
    if not left:
        try:
            (work / "providers.txt").write_text("\n".join(PROVIDERS) + "\n", encoding="ascii")
        except OSError:
            left.append("providers.txt")
    extra = TINY_BUFFERS if tiny_buffers else BUFFERS
    if left:  # never mix an old trace into a new one
        p = subprocess.CompletedProcess([], returncode=-1)
    else:
        p = subprocess.run(["logman", "start", SESSION, "-pf", str(work / "providers.txt"), "-o",
                            str(work / "trace.etl"), *extra, "-ets"], capture_output=True)
    state = {"trace_started": p.returncode == 0, "logman_exit": p.returncode, "stale_raw_left": left, "buffers": extra,
             "canary_start_pid": run_canary("start") if p.returncode == 0 else None}
    (work / "state.json").write_text(json.dumps(state) + "\n", encoding="utf-8")
    return 0 if state["trace_started"] and state["canary_start_pid"] else 2


def stop(work: Path, end_canary: bool = True) -> int:
    try:
        state = json.loads((work / "state.json").read_text(encoding="utf-8"))
        if not isinstance(state, dict):
            raise ValueError
    except (OSError, ValueError):  # still stop the trace; analyze reports the run as not covered
        state = {"state_unreadable": True}
    if end_canary and state.get("trace_started"):
        state["canary_end_pid"] = run_canary("end")
    p = subprocess.run(["logman", "stop", SESSION, "-ets"], capture_output=True)
    state["trace_stopped"] = p.returncode == 0
    if (work / "trace.etl").is_file():
        t = subprocess.run(["tracerpt", str(work / "trace.etl"), "-o", str(work / "trace.xml"), "-of", "XML",
                            "-summary", str(work / "summary.txt"), "-y"], capture_output=True)
        state["decoded"] = t.returncode == 0
    (work / "state.json").write_text(json.dumps(state) + "\n", encoding="utf-8")
    return 0 if state["trace_stopped"] and state.get("decoded") else 2


def abort(work: Path) -> list[str]:
    """An interrupted or failed run (FB-35-04): stop the system-wide trace, delete the raw files, return any left."""
    subprocess.run(["logman", "stop", SESSION, "-ets"], capture_output=True)
    return cleanup(work)


def ip_family(text: str | None) -> str:
    try:
        return f"v{ipaddress.ip_address((text or '').strip()).version}"
    except ValueError:
        return "?"


def addr_class(text: str | None) -> str:
    try:
        a = ipaddress.ip_address((text or "").strip())
    except ValueError:
        return "unknown"
    return "loopback" if a.is_loopback else "private" if a.is_private else "public"


def events(xml_path: Path):
    """(provider, event id, data) per event; data values stripped (tracerpt pads numbers).

    Streamed, each event dropped once read: a whole session's trace did not fit in memory, and one C call parsing the
    whole file never let Ctrl+C through (EXP-051 session 1)."""
    it = None
    try:  # the file is closed on every path, or Windows cannot delete it afterwards (Codex passes 14 and 15)
        it = ET.iterparse(xml_path, events=("start", "end"))
        yield from _events(it)
    finally:
        if it is not None:
            it.close()


def _events(it):
    _, root = next(it, (None, None))
    if root is None:
        return
    tag, depth = f"{{{NS['e']}}}Event", 1
    for kind, ev in it:
        depth += 1 if kind == "start" else -1
        if kind != "end" or depth != 1 or ev.tag != tag:  # only the root's own children, as before (Codex pass 14)
            continue
        s = ev.find("e:System", NS)
        prov = s.find("e:Provider", NS).get("Name")
        data = {d.get("Name"): (d.text or "").strip() for d in ev.findall("e:EventData/e:Data", NS)}
        data["_pid"] = s.find("e:Execution", NS).get("ProcessID", "") if s.find("e:Execution", NS) is not None else ""
        task = ev.find("e:RenderingInfo/e:Task", NS)  # KERNEL_NETWORK_TASK_TCPIP / _UDPIP on this machine
        data["_task"] = (task.text or "") if task is not None else ""
        eid = s.find("e:EventID", NS).text
        root.clear()  # events are the root's children; everything read so far goes
        yield prov, eid, data


def events_lost(summary: str) -> int | None:
    m = re.search(r"Total Events\s+Lost\s+(\d+)", summary)
    return int(m.group(1)) if m else None


def analyze(work: Path, images: list[str], pids: list[int], keep_raw: bool = False) -> dict:
    # A session record with raw_kept or raw_not_deleted is a FAIL (session.py): --keep-raw is for debugging only.
    out = {"raw_not_deleted": [], "raw_kept": keep_raw}
    try:
        out.update(_analyze(work, images, pids))
        return out
    finally:  # the raw trace holds every process's traffic: never left behind, whatever the outcome
        if not keep_raw:
            out["raw_not_deleted"] = cleanup(work)


def _analyze(work: Path, images: list[str], pids: list[int]) -> dict:
    problems, out = [], {"monitor": "ETW via logman/tracerpt", "providers": PROVIDERS}
    out.update(contacts=[], dns=[], targets=[], coverage="not_covered", coverage_problems=problems, verdict="not_covered")
    try:
        state = json.loads((work / "state.json").read_text(encoding="utf-8"))
        if not isinstance(state, dict):
            raise ValueError
    except (OSError, ValueError):
        state = {}
        problems.append("no readable trace state")
    if not state.get("trace_started"):
        problems.append("the trace did not start (an elevated prompt is required)")
    if state.get("trace_stopped") is not True or state.get("decoded") is not True:
        problems.append("the trace was not stopped and decoded cleanly")
    def pid_of(d):
        v = d.get("PID", "") if "PID" in d else d.get("_pid", "")
        return int(v) if v.isdigit() else None

    # One pass that keeps only counts: the targets are known only at the end, so contacts and lookups are counted per
    # PID for every process, which grows with distinct flows, not with events (EXP-051 session 1).
    procs, pid_seqs, total = {}, {}, 0  # process instances by sequence number, which, unlike a PID, is never reused
    canary_pids = {t: state.get(f"canary_{t}_pid") for t in ("start", "end")}
    canary_kinds = {t: set() for t in canary_pids}
    flows, lookups = {}, {}  # pid -> {flow key: count}, pid -> {name: count}
    stream = None
    try:
        stream = events(work / "trace.xml")
        for prov, eid, d in stream:
            total += 1
            if prov == PROC and eid in (PROC_START, PROC_STOP) and d.get("ProcessID", "").isdigit() \
                    and d.get("ProcessSequenceNumber", "").isdigit():
                seq, pid = int(d["ProcessSequenceNumber"]), int(d["ProcessID"])
                p = procs.setdefault(seq, {"pid": pid, "image": "", "started": False, "exited": False,
                                           "exit_code": None, "parent_seq": None})
                p["image"] = p["image"] or d.get("ImageName", "").replace("/", "\\").rsplit("\\", 1)[-1]
                pid_seqs.setdefault(pid, set()).add(seq)
                if eid == PROC_START:
                    p["started"] = True
                    if d.get("ParentProcessSequenceNumber", "").isdigit():
                        p["parent_seq"] = int(d["ParentProcessSequenceNumber"])
                else:
                    p["exited"], p["exit_code"] = True, d.get("ExitCode")
            pid = pid_of(d)
            for t, cpid in canary_pids.items():
                if not cpid or pid != cpid:
                    continue
                if prov == NET and eid in (TCP_CONNECT, UDP_SEND) and addr_class(d.get("daddr")) == "loopback":
                    canary_kinds[t].add(eid)
                if prov == DNS and eid == DNS_QUERY and d.get("QueryName") == canary_name(t):
                    canary_kinds[t].add(eid)
            if prov == NET:
                peer = max((addr_class(d.get("daddr")), addr_class(d.get("saddr"))),
                           key=["loopback", "private", "public", "unknown"].index)
                task = d["_task"].upper()
                proto = "tcp" if "TCP" in task else "udp" if "UDP" in task else "?"
                # both ports: which one is remote depends on direction
                key = (eid, proto, ip_family(d.get("daddr")), peer, d.get("sport", ""), d.get("dport", ""))
                f = flows.setdefault(pid, {})
                f[key] = f.get(key, 0) + 1
            elif prov == DNS and eid == DNS_QUERY:
                n = lookups.setdefault(pid, {})
                n[d.get("QueryName", "")] = n.get(d.get("QueryName", ""), 0) + 1
    except (OSError, ET.ParseError, AttributeError) as e:  # missing, truncated or not an ETW dump
        # Read in order now: a file both malformed and truncated names the first fault met (Codex pass 14, P3).
        problems.append(f"no decodable trace ({type(e).__name__})")
        return out
    finally:  # an interrupt between events still closes the file before analyze deletes it
        if stream is not None:
            stream.close()
    try:
        lost = events_lost((work / "summary.txt").read_text(encoding="utf-8", errors="replace"))
    except OSError:
        lost = None  # unknown loss is not zero loss
    out["events_lost"], out["events_total"], out["buffers"] = lost, total, state.get("buffers")
    if lost != 0:
        problems.append(f"events lost: {lost}")
    out["canaries"] = {t: k == {TCP_CONNECT, UDP_SEND, DNS_QUERY} for t, k in canary_kinds.items()}
    problems += [f"{t} canary not fully seen" for t, ok in out["canaries"].items() if not ok]

    want = {i.lower() for i in images}
    roots = {s for s, p in procs.items() if p["image"].lower() in want or p["pid"] in pids}
    targets = set(roots)
    grew = True
    while grew:  # child processes of a target (a crash reporter, a launcher) are targets too
        new = {s for s, p in procs.items() if p["parent_seq"] in targets} - targets
        targets |= new
        grew = bool(new)
    out["targets"] = [{"image": procs[s]["image"], "started_in_trace": procs[s]["started"],
                       "exited_in_trace": procs[s]["exited"], "exit_code": procs[s]["exit_code"]} for s in sorted(targets)]
    if not roots:
        problems.append("no target process seen in the trace")
    problems += [f"requested target PID {p} has no process in the trace" for p in pids if p not in pid_seqs]
    problems += [f"requested target image {i} has no process in the trace" for i in images
                 if not any(procs[s]["image"].lower() == i.lower() for s in roots)]
    for t in out["targets"]:
        if not (t["started_in_trace"] and t["exited_in_trace"]):
            problems.append(f"target {t['image']} not fully inside the trace window")
    target_pids = {procs[s]["pid"] for s in targets} | set(pids)
    child_pids = {procs[s]["pid"] for s in targets - roots}
    if any(len(pid_seqs.get(p, ())) > 1 for p in target_pids):  # network events carry only the PID
        problems.append("a target PID was reused during the trace: attribution is ambiguous")

    contacts, dns = {}, {}
    for pid in target_pids:
        for key, n in flows.get(pid, {}).items():
            k = (*key, pid in child_pids)
            contacts[k] = contacts.get(k, 0) + n
        for name, n in lookups.get(pid, {}).items():
            dns[name] = dns.get(name, 0) + n
    out["contacts"] = [{"event_id": e, "proto": pr, "ip": fam, "peer": p, "sport": sp, "dport": dp, "from_child": ch,
                        "count": n} for (e, pr, fam, p, sp, dp, ch), n in sorted(contacts.items())]
    out["dns"] = [{"name": k, "count": n} for k, n in sorted(dns.items())]
    unexpected = [c for c in out["contacts"] if c["peer"] != "loopback"]
    out["coverage"] = "covered" if not problems else "not_covered"
    # Any lookup or any non-loopback traffic by a target is unexpected online contact (stop condition 4). A contact
    # that was seen is a FAIL even if coverage is incomplete: losing other events never hides one that was recorded.
    out["verdict"] = "unexpected_contact" if unexpected or out["dns"] else "not_covered" if problems else "clean"
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("cmd", choices=["start", "stop", "analyze", "canary", "cleanup"])
    ap.add_argument("tag", nargs="?")
    ap.add_argument("--work", type=Path)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--image", action="append", default=[])
    ap.add_argument("--pid", action="append", type=int, default=[])
    ap.add_argument("--no-end-canary", action="store_true", help="dry run only: simulate lost coverage")
    ap.add_argument("--keep-raw", action="store_true")
    a = ap.parse_args(argv)
    if a.cmd == "canary":
        print(canary(a.tag or "x"))
        return 0
    if a.cmd == "start":
        return start(a.work)
    if a.cmd == "cleanup":  # after an interrupted run
        left = cleanup(a.work)
        print("raw trace deleted" if not left else f"NOT deleted: {left}")
        return 0 if not left else 2
    if a.cmd == "stop":
        return stop(a.work, not a.no_end_canary)
    r = analyze(a.work, a.image, a.pid, a.keep_raw)
    a.out.write_text(json.dumps(r, indent=2) + "\n", encoding="utf-8")
    print(f"network {r['verdict']}: {r['coverage']}" + "".join(f"\n  - {p}" for p in r["coverage_problems"]))
    return 0 if r["verdict"] == "clean" else 3 if r["verdict"] == "unexpected_contact" else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
