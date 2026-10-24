"""KI-35 diagnostics: phase timeline of each stalled client's secure reconnect in a soak run directory.

Reads host.log and client*.log (JSON lines). Client t_ms is mapped onto the shared steady clock through the
`clock` record; `dtls_trace` records carry it directly. Prints one line per reconnect:
  detect   stall_end -> first ClientHello of the new handshake
  dtls     first ClientHello -> client handshake_done (cookie exchange + DTLS handshake)
  binding  handshake_done -> client authenticated (invitation binding)
  session  client authenticated -> reconnected (ConnectRequest / ConnectAccept)
plus handshake timer firings and binding counts. Read-only; nothing is written.
Usage: python -I tools/soak/timeline.py <run_dir> [<run_dir> ...]
"""
from __future__ import annotations

import json
import sys
from pathlib import Path


def read(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def mono_ms(ev: dict, base_us: int) -> float:
    return ev["mono_us"] / 1000 if "mono_us" in ev and ev.get("evt") == "dtls_trace" else (base_us / 1000 + ev["t_ms"])


def reconnects(client: list[dict], host: list[dict]) -> list[dict]:
    clock = next((e for e in client if e.get("evt") == "clock"), None)
    if not clock:
        return []
    base = clock["mono_us"]
    out = []
    for i, e in enumerate(client):
        if e.get("evt") != "stall_end":
            continue
        t0 = mono_ms(e, base)
        rest = client[i + 1:]
        trace = [x for x in rest if x.get("evt") == "dtls_trace"]
        first_ch = next((x for x in trace if x["ev"] == "tx" and x["detail"].split(",")[0] == "22/0/1"), None)
        done = next((x for x in trace if x["ev"] == "handshake_done"), None)
        auth = next((x for x in trace if x["ev"] == "authenticated"), None)
        rec = next((x for x in rest if x.get("evt") == "reconnected"), None)
        if not (first_ch and done and auth and rec):
            out.append({"incomplete": True})
            continue
        ch, d, a, r = (mono_ms(x, base) for x in (first_ch, done, auth, rec))
        between = [x for x in trace if ch <= mono_ms(x, base) <= a]
        host_win = [x for x in host if x.get("evt") == "dtls_trace" and ch <= x["mono_us"] / 1000 <= r]
        reqs = [x for x in rest if x.get("evt") == "connect_request" and mono_ms(x, base) <= mono_ms(rec, base)]
        inner = next((x for x in trace if x["ev"] == "inner_connected"), None)  # connection-oriented inner only
        inner_ms = mono_ms(inner, base) if inner else None
        is_ch = lambda x: x["ev"] == "tx" and x["detail"].split(",")[0] == "22/0/1"
        out.append({
            "inner_connected_after_first_hello": None if inner_ms is None else inner_ms - ch,
            "hellos_before_inner_connected": None if inner_ms is None else sum(is_ch(x) and mono_ms(x, base) < inner_ms for x in trace),
            "session_requests": len(reqs),
            "session_requests_after_auth": sum(mono_ms(x, base) >= a - 2 for x in reqs),  # log t_ms is 1 ms resolution
            "detect": ch - t0, "dtls": d - ch, "binding": a - d, "session": r - a, "total": r - t0,
            "reconnect_ms": rec.get("reconnect_ms"),
            "client_timers": sum(x["ev"] == "timer" for x in between),
            "client_hellos": sum(x["ev"] == "tx" and x["detail"].split(",")[0] == "22/0/1" for x in between),
            "client_bindings_sent": sum(x["ev"] == "tx" and "23/1" in x["detail"].split(",") for x in between),
            "host_timers": sum(x["ev"] == "timer" for x in host_win),
            "host_binding_replies": sum(x["ev"] == "binding_reply" for x in host_win),
            "host_binding_suppressed": sum(x["ev"] == "binding_reply_suppressed" for x in host_win),
        })
    return out


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2
    for d in map(Path, argv):
        host = read(d / "host.log") if (d / "host.log").is_file() else []
        for c in sorted(d.glob("client*.log")):
            for r in reconnects(read(c), host):
                if r.get("incomplete"):
                    print(f"{d.name} {c.stem}: incomplete reconnect trace")
                    continue
                print(f"{d.name} {c.stem}: total {r['total']:.0f} ms = detect {r['detect']:.0f} + dtls {r['dtls']:.0f} "
                      f"+ binding {r['binding']:.0f} + session {r['session']:.0f} | client timers {r['client_timers']}, "
                      f"hellos {r['client_hellos']}, bindings sent {r['client_bindings_sent']} | host timers "
                      f"{r['host_timers']}, binding replies {r['host_binding_replies']} "
                      f"(suppressed {r['host_binding_suppressed']}) | reconnect_ms {r['reconnect_ms']} | inner connected "
                      f"{r['inner_connected_after_first_hello']} ms after the first hello, hellos before it "
                      f"{r['hellos_before_inner_connected']} | ConnectRequests {r['session_requests']} "
                      f"({r['session_requests_after_auth']} after authentication)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
