"""Stand-in for the game in the M1 dry run (EXP-045). It is NOT the game: it appends scripted positions to a fixture
log, optionally sends one UDP datagram to TEST-NET-1 (192.0.2.1, RFC 5737: documentation only, never routed), and
exits with the given code.  python -I tools/m1/standin.py --log FILE --mode moving|static|none [--contact] [--exit N]
"""
import argparse
import socket
import sys
import time

a = argparse.ArgumentParser()
a.add_argument("--log", required=True)
a.add_argument("--mode", choices=["moving", "static", "none"], default="moving")
a.add_argument("--contact", action="store_true")
a.add_argument("--exit", type=int, default=0)
o = a.parse_args()
lines = []
if o.mode != "none":  # session time in seconds: standing still for 1-3, walking for 11-13
    lines += [f"pos t={t} 0.0 0.0 0.0" for t in (1, 2, 3)]
    lines += [f"pos t={t} {(t * 2.0 if o.mode == 'moving' else 0.0)} 0.0 0.0" for t in (11, 12, 13)]
with open(o.log, "a", encoding="utf-8") as f:  # the stand-in's own log, like a game writing its log
    f.write("".join(x + "\n" for x in lines))
if o.contact:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.sendto(b"standin", ("192.0.2.1", 9))
    s.close()
time.sleep(0.5)
sys.exit(o.exit)
