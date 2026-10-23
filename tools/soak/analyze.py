"""Analyze one real-time soak run (wall clock, separate processes) and evaluate fixed pass gates.

Inputs, all in <run_dir>:
  run.json      written by scripts/soak.ps1 (profile, impairment, stall schedule, exit codes)
  host.log      JSON lines from mordor-host
  client*.log   JSON lines from each mordor-client
Output: <run_dir>/summary.json, plus a printed table. Exit code 1 if any gate fails.

Gates were fixed before the first soak run (see docs/soak-testing.md); do not tune them to results.
Usage: python -I tools/soak/analyze.py <run_dir>
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

# Real-time loops sleep ~1 ms but Windows timer granularity is ~15.6 ms, applied on both sides (KI-02).
RTT_SCHEDULING_ALLOWANCE_MS = 60.0
TICK_DRIFT_MAX = 0.01            # |ticks - expected| / expected
RECOVERY_AFTER_STALL_MAX_MS = 3000.0


def read_log(path: Path) -> list[dict]:
    events = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError:
            events.append({"evt": "_unparseable", "raw": line[:200]})
    return events


def last(events: list[dict], evt: str) -> dict | None:
    return next((e for e in reversed(events) if e.get("evt") == evt), None)


def recovery_ms(events: list[dict]) -> float | None:
    """Wall time from the end of the stall to the first 'reconnected' event after it."""
    end = next((e for e in events if e.get("evt") == "stall_end"), None)
    if end is None:
        return None
    rec = next((e for e in events if e.get("evt") == "reconnected" and e["t_ms"] >= end["t_ms"]), None)
    return None if rec is None else float(rec["t_ms"] - end["t_ms"])


def analyze(run_dir: Path) -> dict:
    run = json.loads((run_dir / "run.json").read_text(encoding="utf-8"))
    imp = run["impairment"]
    host = read_log(run_dir / "host.log")
    clients = {p.stem: read_log(p) for p in sorted(run_dir.glob("client*.log"))}
    gates: list[dict] = []

    def gate(name: str, ok: bool, detail: str) -> None:
        gates.append({"gate": name, "passed": bool(ok), "detail": detail})

    hs = last(host, "host_stopped") or {}
    gate("host_reported_stop", bool(hs), "host_stopped event present")
    gate("host_exit_code", run["exit_codes"].get("host") == 0, f"exit {run['exit_codes'].get('host')}")
    gate("host_invalid_packets_zero", hs.get("invalid", -1) == 0, f"invalid={hs.get('invalid')}")
    expected = hs.get("uptime_us", 0) / 1e6 * hs.get("tick_rate_hz", 0)
    ticks = hs.get("world_tick", 0) + hs.get("skipped_ticks", 0)
    drift = abs(ticks - expected) / expected if expected else 1.0
    gate("host_tick_drift", drift <= TICK_DRIFT_MAX, f"drift={drift:.5f} (ticks={ticks}, expected={expected:.1f})")
    gate("host_never_skipped_ticks", hs.get("skipped_ticks", -1) == 0, f"skipped_ticks={hs.get('skipped_ticks')}")

    configured_rtt = 2.0 * (imp["latency_ms"] + imp["jitter_ms"])  # worst-case configured delay, both directions
    rtt_limit = configured_rtt + RTT_SCHEDULING_ALLOWANCE_MS
    stalled = set(run.get("stalled_clients", []))
    per_client = {}
    for name, ev in clients.items():
        s = last(ev, "summary") or {}
        rec = recovery_ms(ev)
        is_stalled = name in stalled
        per_client[name] = {**{k: s.get(k) for k in ("final_state", "player", "rtt_samples", "rtt_p50_ms", "rtt_p95_ms",
                                                      "rtt_p99_ms", "rtt_max_ms", "snapshots", "stale", "corrections",
                                                      "max_correction", "reconnects", "max_reconnect_ms", "invalid",
                                                      "bytes_in", "bytes_out", "elapsed_ms")},
                            "stalled": is_stalled, "recovery_after_stall_ms": rec,
                            "kbit_s_in": round(s.get("bytes_in", 0) * 8 / max(s.get("elapsed_ms", 1), 1), 2),
                            "kbit_s_out": round(s.get("bytes_out", 0) * 8 / max(s.get("elapsed_ms", 1), 1), 2)}
        gate(f"{name}_summary_present", bool(s), "summary event present")
        gate(f"{name}_exit_code", run["exit_codes"].get(name) == 0, f"exit {run['exit_codes'].get(name)}")
        gate(f"{name}_final_connected", s.get("final_state") == "connected", f"final_state={s.get('final_state')}")
        gate(f"{name}_invalid_zero", s.get("invalid", -1) == 0, f"invalid={s.get('invalid')}")
        p99 = s.get("rtt_p99_ms", -1)
        gate(f"{name}_rtt_p99", 0 <= p99 <= rtt_limit, f"p99={p99} ms, limit={rtt_limit} ms (configured {configured_rtt} + {RTT_SCHEDULING_ALLOWANCE_MS})")
        if is_stalled:
            gate(f"{name}_reconnected_once", s.get("reconnects") == 1, f"reconnects={s.get('reconnects')}")
            gate(f"{name}_recovery_time", rec is not None and rec <= RECOVERY_AFTER_STALL_MAX_MS,
                 f"recovery={rec} ms, limit={RECOVERY_AFTER_STALL_MAX_MS} ms")
        else:
            gate(f"{name}_no_reconnects", s.get("reconnects") == 0, f"reconnects={s.get('reconnects')}")

    n_stalled = len(stalled & set(clients))
    if run.get("stall_s", 0) * 1000 > run.get("host_timeout_ms", 3000) and n_stalled:
        # A stall longer than the host timeout must be seen as a timeout and recovered via the token (same identity).
        gate("host_timeouts_match_stalls", hs.get("timeouts") == n_stalled, f"timeouts={hs.get('timeouts')}")
        gate("host_reconnects_match_stalls", hs.get("reconnects") == n_stalled, f"reconnects={hs.get('reconnects')}")
    gate("host_joins_match_clients", hs.get("joins") == len(clients), f"joins={hs.get('joins')}, clients={len(clients)}")

    return {
        "clock": "real-time (wall clock, separate processes, loopback)",
        "run": run,
        "host": {k: hs.get(k) for k in ("world_tick", "uptime_us", "skipped_ticks", "shed_inputs", "stale_inputs", "joins",
                                        "reconnects", "timeouts", "invalid", "unknown_peer", "bytes_in", "bytes_out")},
        "host_tick_drift": round(drift, 6),
        "clients": per_client,
        "gates": gates,
        "passed": all(g["passed"] for g in gates),
    }


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print(__doc__)
        return 2
    run_dir = Path(argv[0])
    try:
        result = analyze(run_dir)
    except (OSError, KeyError, json.JSONDecodeError) as e:
        print(f"error: cannot analyze {run_dir}: {e}", file=sys.stderr)
        return 2
    (run_dir / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for g in result["gates"]:
        print(f"[{'PASS' if g['passed'] else 'FAIL'}] {g['gate']}: {g['detail']}")
    for name, c in result["clients"].items():
        print(f"  {name}: rtt p50/p95/p99/max = {c['rtt_p50_ms']}/{c['rtt_p95_ms']}/{c['rtt_p99_ms']}/{c['rtt_max_ms']} ms, "
              f"corrections={c['corrections']} (max {c['max_correction']}), stale={c['stale']}, "
              f"in/out={c['kbit_s_in']}/{c['kbit_s_out']} kbit/s, recovery={c['recovery_after_stall_ms']} ms")
    print(f"  host: drift={result['host_tick_drift']}, shed_inputs={result['host']['shed_inputs']}, "
          f"timeouts={result['host']['timeouts']}, reconnects={result['host']['reconnects']}")
    print("SOAK PASSED" if result["passed"] else "SOAK FAILED")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
