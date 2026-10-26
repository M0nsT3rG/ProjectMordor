"""Gates for the separate-process asymmetric scenarios (review #6 FB-6-05; scripts/asymmetric.ps1).

With GNS, fake impairment is process-global, so asymmetric cases run as separate processes, each with its own
impairment. Fixed expectations, decided before the first run:
  - every process exits 0;
  - the host times out exactly the one client whose uplink was cut (timeouts == 1);
  - the unaffected client never reconnects and ends connected;
  - the cut client reconnects at least once, keeps its player id, ends connected, and its longest reconnect is
    bounded by the outage plus one host timeout and one retry window (outage_s + 4 s);
  - scenario host-jitter-client-outage: the unaffected client's RTT p50 shows the host-only delay (>= latency_ms).
Writes check.json next to the logs; refuses to overwrite an existing one (evidence is immutable).
Usage: python -I tools/e2e/check_asymmetric.py <run_dir>
"""
import json
import sys
from pathlib import Path


def events(log: Path):
    out = []
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            out.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return out


def last(evts, name):
    found = [e for e in evts if e.get("evt") == name]
    return found[-1] if found else None


def first_player(evts):
    for e in evts:
        if e.get("evt") == "status" and e.get("player", 0):
            return e["player"]
    return None


def check(run_dir: Path) -> dict:
    run = json.loads((run_dir / "run.json").read_text(encoding="utf-8-sig"))
    host = events(run_dir / "host.log")
    c = {n: events(run_dir / f"{n}.log") for n in ("client1", "client2")}
    gates = []

    def gate(name, ok, detail):
        gates.append({"gate": name, "passed": bool(ok), "detail": detail})

    for name, code in run["exit_codes"].items():
        gate(f"{name}_exit_0", code == 0, code)
    stopped = last(host, "host_stopped") or {}
    gate("host_transport_matches", stopped.get("transport") == run["transport"], stopped.get("transport"))
    gate("host_timed_out_exactly_the_cut_client", stopped.get("timeouts") == 1, stopped.get("timeouts"))
    s1, s2 = last(c["client1"], "summary") or {}, last(c["client2"], "summary") or {}
    gate("client1_never_reconnected", s1.get("reconnects") == 0, s1.get("reconnects"))
    gate("client1_ends_connected", s1.get("final_state") == "connected", s1.get("final_state"))
    gate("client2_reconnected", (s2.get("reconnects") or 0) >= 1, s2.get("reconnects"))
    gate("client2_ends_connected", s2.get("final_state") == "connected", s2.get("final_state"))
    gate("client2_kept_identity", s2.get("player") is not None and s2.get("player") == first_player(c["client2"]),
         [first_player(c["client2"]), s2.get("player")])
    bound = int(run["outage_s"] * 1000) + 4000
    gate("client2_reconnect_bounded", 0 < (s2.get("max_reconnect_ms") or 0) <= bound, [s2.get("max_reconnect_ms"), bound])
    gate("client2_outage_happened", last(c["client2"], "outage_end") is not None, None)
    if run["scenario"] == "host-jitter-client-outage":
        lat = run["host_impairment"]["latency_ms"]
        gate("client1_sees_host_only_delay", (s1.get("rtt_p50_ms") or 0) >= lat, [s1.get("rtt_p50_ms"), lat])
    return {"scenario": run["scenario"], "transport": run["transport"], "passed": all(g["passed"] for g in gates), "gates": gates}


def main() -> int:
    run_dir = Path(sys.argv[1])
    out = run_dir / "check.json"
    if out.exists():
        print(f"{out} exists; refusing to overwrite evidence", file=sys.stderr)
        return 2
    result = check(run_dir)
    out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for g in result["gates"]:
        print(f"{'PASS' if g['passed'] else 'FAIL'} {g['gate']} {g['detail']}")
    print(("ASYMMETRIC PASSED" if result["passed"] else "ASYMMETRIC FAILED") + f" ({result['scenario']}, {result['transport']})")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
