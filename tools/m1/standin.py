"""Stand-in for the game in the M1 dry run and coverage experiment (EXP-045, EXP-046). It is NOT the game.

It appends scripted positions to a fixture log, then performs the network actions it is given, and exits with the
given code. Public destinations are documentation addresses only (RFC 5737 192.0.2.1, RFC 3849 2001:db8::1), never
routed; names are .invalid (RFC 6761). `--status FILE` records which actions the OS let through.
  python -I tools/m1/standin.py --log FILE [--mode moving|static|none] [--contact] [--udp HOST]... [--tcp HOST]...
         [--dns NAME]... [--loop6] [--brief N] [--flood N] [--child-udp HOST] [--status FILE] [--exit N]
"""
import argparse
import json
import socket
import subprocess
import sys
import time

a = argparse.ArgumentParser()
a.add_argument("--log", required=True)
a.add_argument("--mode", choices=["moving", "static", "none"], default="moving")
a.add_argument("--contact", action="store_true", help="one UDP datagram to 192.0.2.1 (EXP-045)")
a.add_argument("--udp", action="append", default=[])
a.add_argument("--tcp", action="append", default=[])
a.add_argument("--dns", action="append", default=[])
a.add_argument("--loop6", action="store_true", help="a TCP connection and a UDP datagram on ::1")
a.add_argument("--brief", type=int, default=0, help="N very short loopback TCP connections")
a.add_argument("--flood", type=int, default=0, help="N loopback UDP datagrams")
a.add_argument("--child-udp", help="a child process sends one UDP datagram to this host")
a.add_argument("--status")
a.add_argument("--exit", type=int, default=0)
o = a.parse_args()
status = {}


def attempt(name, fn):
    try:
        status[name] = fn() or "sent"
    except OSError as e:
        status[name] = f"refused by the OS ({getattr(e, 'winerror', None) or e.errno})"


def udp(host):
    fam = socket.AF_INET6 if ":" in host else socket.AF_INET
    with socket.socket(fam, socket.SOCK_DGRAM) as s:
        s.sendto(b"standin", (host, 9))


def tcp(host):
    fam = socket.AF_INET6 if ":" in host else socket.AF_INET
    with socket.socket(fam, socket.SOCK_STREAM) as s:
        s.settimeout(1.0)
        try:
            s.connect((host, 9))
        except (TimeoutError, ConnectionError):
            return "attempted, never connected"  # the attempt is what is traced
        return "connected"


def loop_tcp(host):
    fam = socket.AF_INET6 if ":" in host else socket.AF_INET
    with socket.socket(fam, socket.SOCK_STREAM) as srv:
        srv.bind((host, 0))
        srv.listen(1)
        with socket.create_connection(srv.getsockname()[:2], timeout=2) as c:
            peer, _ = srv.accept()
            c.sendall(b"x")
            peer.recv(4)
            peer.close()


lines = []
if o.mode != "none":  # session time in seconds: standing still for 1-3, walking for 11-13
    lines += [f"pos t={t} 0.0 0.0 0.0" for t in (1, 2, 3)]
    lines += [f"pos t={t} {(t * 2.0 if o.mode == 'moving' else 0.0)} 0.0 0.0" for t in (11, 12, 13)]
with open(o.log, "a", encoding="utf-8") as f:  # the stand-in's own log, like a game writing its log
    f.write("".join(x + "\n" for x in lines))
if o.loop6:
    attempt("loop6_tcp", lambda: loop_tcp("::1"))
    attempt("loop6_udp", lambda: udp("::1"))
for i in range(o.brief):
    attempt("brief", lambda: loop_tcp("127.0.0.1"))
if o.flood:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        for _ in range(o.flood):
            s.sendto(b"f", ("127.0.0.1", 9))
    status["flood"] = o.flood
for n in o.dns:
    try:
        socket.getaddrinfo(n, 80)
    except OSError:
        pass
    status[f"dns {n}"] = "asked"
for h in o.tcp:
    attempt(f"tcp {h}", lambda h=h: tcp(h))
for h in o.udp + (["192.0.2.1"] if o.contact else []):
    attempt(f"udp {h}", lambda h=h: udp(h))
if o.child_udp:
    child_status = (o.status or o.log) + ".child.json"
    child = subprocess.run([sys.executable, "-I", __file__, "--log", o.log, "--mode", "none", "--udp", o.child_udp,
                            "--status", child_status], timeout=30)
    status["child"] = child.returncode
    try:
        with open(child_status, encoding="utf-8") as f:
            status.update({f"child {k}": v for k, v in json.load(f).items()})  # a refused send stays visible
    except (OSError, ValueError):
        status["child status"] = "refused by the OS (no status from the child)"
if o.status:
    with open(o.status, "w", encoding="utf-8") as f:
        json.dump(status, f)
time.sleep(0.5)
sys.exit(o.exit)
