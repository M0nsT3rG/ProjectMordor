"""Analyze one real-time soak run (wall clock, separate processes) and evaluate fixed pass gates.

Inputs, all in <run_dir>:
  run.json      written by scripts/soak.ps1: profile, impairment, stall schedule, expected_clients, exit codes
  host.log      JSON lines from mordor-host
  client*.log   JSON lines from each mordor-client
Output: <run_dir>/summary.json (never overwritten; evidence is immutable), plus a printed table.
Exit code 1 if any gate fails, 2 on usage/IO errors or if summary.json already exists.

Gates are versioned. A gate version is fixed before any run that uses it (docs/soak-testing.md) and is
never tuned to results; a stricter or different gate is a new version, and old summaries keep theirs.
  v1 (2026-10-23): exit codes, final state, invalid packets, drift, RTT p99, stall recovery, bookkeeping.
  v2 (2026-10-23, review #2): + exact participant manifest (FB-2-01), duration and sample minimums,
     design-derived bandwidth and correction ceilings (FB-2-03).
Usage: python -I tools/soak/analyze.py <run_dir>
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

GATE_VERSION = 2

# Synthetic loopback regression bound, not a LAN latency criterion (FB-2-04). Kept at the v1 value for
# comparability. Rationale at v1: ~1 ms sleeps with ~15.6 ms Windows timer granularity on both sides; since
# the PreciseSleeper fix (KI-02) the measured overhead is ~4 ms, so this bound is now conservative.
RTT_SCHEDULING_ALLOWANCE_MS = 60.0
TICK_DRIFT_MAX = 0.01  # |ticks - expected| / expected
RECOVERY_AFTER_STALL_MAX_MS = 3000.0

# --- v2 constants, derived from the protocol design (src/network/protocol.hpp), not from measurements ---
HEADER = 22
SNAPSHOT_FIXED = 4 + 4 + 1          # server_tick, last_input_seq, entity count
SNAPSHOT_PER_ENTITY = 28
INPUT_FIXED, INPUT_PER_CMD, MAX_INPUTS = 1, 12, 8
HEARTBEAT = 8                       # Heartbeat / HeartbeatAck payload
HEARTBEATS_PER_S = 4                # client heartbeat_interval 250 ms
TICK_RATE = 30
MOVE_SPEED = 5.0                    # sim::kMoveSpeed, units per second
TARGET_BACKLOG_TICKS = 3            # host keeps <= 2 queued + 1 applied (D-010)
BANDWIDTH_MARGIN = 1.05             # tick catch-up after stalls (<= 5 ticks) and start/stop edges
DURATION_MIN_FRACTION = 0.95
RTT_SAMPLE_MIN_FRACTION = 0.5       # of 4 heartbeats/s, before loss (<= 15% each way in the profiles)


def download_budget_kbit(entities: int, duplicate: float) -> float:
    """Upper bound of host->client payload per client: one snapshot per tick plus heartbeat acks, duplicated."""
    snapshot = HEADER + SNAPSHOT_FIXED + SNAPSHOT_PER_ENTITY * entities
    bps = (snapshot * TICK_RATE + (HEADER + HEARTBEAT) * HEARTBEATS_PER_S) * (1 + duplicate)
    return bps * 8 / 1000 * BANDWIDTH_MARGIN


def upload_budget_kbit() -> float:
    """Upper bound of offered client->host payload: one full input batch per tick plus heartbeats (pre-loss)."""
    bps = (HEADER + INPUT_FIXED + INPUT_PER_CMD * MAX_INPUTS) * TICK_RATE + (HEADER + HEARTBEAT) * HEARTBEATS_PER_S
    return bps * 8 / 1000 * BANDWIDTH_MARGIN


def correction_budget(rtt_limit_ms: float) -> float:
    """Reconciliation only replays unacknowledged inputs: at most one RTT of predicted movement plus the
    host's bounded input backlog, so no correction can exceed the movement possible in that time."""
    return MOVE_SPEED * (rtt_limit_ms / 1000.0 + TARGET_BACKLOG_TICKS / TICK_RATE)


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
    host = read_log(run_dir / "host.log") if (run_dir / "host.log").is_file() else []
    found = {p.stem: read_log(p) for p in sorted(run_dir.glob("client*.log"))}
    gates: list[dict] = []

    def gate(name: str, ok: bool, detail: str) -> None:
        gates.append({"gate": name, "passed": bool(ok), "detail": detail})

    # FB-2-01: the participant set comes from the manifest, never from whatever logs happen to exist.
    expected = run.get("expected_clients")
    gate("manifest_lists_expected_clients", isinstance(expected, list) and len(expected) > 0,
         f"expected_clients={expected!r}")
    expected = list(expected) if isinstance(expected, list) else []
    missing, extra = sorted(set(expected) - set(found)), sorted(set(found) - set(expected))
    gate("client_logs_match_manifest", not missing and not extra, f"missing={missing}, unexpected={extra}")
    stalled = set(run.get("stalled_clients", []))
    gate("stalled_clients_in_manifest", stalled <= set(expected), f"stalled={sorted(stalled)}")
    exit_codes = run.get("exit_codes", {})
    gate("exit_codes_cover_all_processes", set(exit_codes) == {"host", *expected},
         f"exit_codes for {sorted(exit_codes)}")

    hs = last(host, "host_stopped") or {}
    gate("host_reported_stop", bool(hs), "host_stopped event present")
    gate("host_exit_code", exit_codes.get("host") == 0, f"exit {exit_codes.get('host')}")
    gate("host_invalid_packets_zero", hs.get("invalid", -1) == 0, f"invalid={hs.get('invalid')}")
    uptime_s = hs.get("uptime_us", 0) / 1e6
    expected_ticks = uptime_s * hs.get("tick_rate_hz", 0)
    ticks = hs.get("world_tick", 0) + hs.get("skipped_ticks", 0)
    drift = abs(ticks - expected_ticks) / expected_ticks if expected_ticks else 1.0
    gate("host_tick_drift", drift <= TICK_DRIFT_MAX, f"drift={drift:.5f} (ticks={ticks}, expected={expected_ticks:.1f})")
    gate("host_never_skipped_ticks", hs.get("skipped_ticks", -1) == 0, f"skipped_ticks={hs.get('skipped_ticks')}")
    duration_s = float(run.get("duration_s", 0))
    gate("host_ran_full_duration", uptime_s >= duration_s, f"uptime={uptime_s:.1f} s, duration={duration_s} s")

    configured_rtt = 2.0 * (imp["latency_ms"] + imp["jitter_ms"])  # worst-case configured delay, both directions
    rtt_limit = configured_rtt + RTT_SCHEDULING_ALLOWANCE_MS
    down_budget = download_budget_kbit(len(expected), imp.get("duplicate", 0.0))
    up_budget = upload_budget_kbit()
    corr_budget = correction_budget(rtt_limit)
    per_client = {}
    for name in expected:  # every expected client is gated, present or not
        ev = found.get(name, [])
        s = last(ev, "summary") or {}
        rec = recovery_ms(ev)
        is_stalled = name in stalled
        elapsed_s = s.get("elapsed_ms", 0) / 1000
        kin = round(s.get("bytes_in", 0) * 8 / max(s.get("elapsed_ms", 1), 1), 2)
        kout = round(s.get("bytes_out", 0) * 8 / max(s.get("elapsed_ms", 1), 1), 2)
        per_client[name] = {**{k: s.get(k) for k in ("final_state", "player", "rtt_samples", "rtt_p50_ms", "rtt_p95_ms",
                                                      "rtt_p99_ms", "rtt_max_ms", "snapshots", "stale", "corrections",
                                                      "max_correction", "correction_p50", "correction_p99",
                                                      "reconnects", "max_reconnect_ms", "stale_notices", "invalid",
                                                      "bytes_in", "bytes_out", "elapsed_ms")},
                            "stalled": is_stalled, "recovery_after_stall_ms": rec, "kbit_s_in": kin, "kbit_s_out": kout,
                            "high_resolution_timer": (last(ev, "loop_timer") or {}).get("high_resolution")}
        gate(f"{name}_summary_present", bool(s), "summary event present")
        gate(f"{name}_exit_code", exit_codes.get(name) == 0, f"exit {exit_codes.get(name)}")
        gate(f"{name}_final_connected", s.get("final_state") == "connected", f"final_state={s.get('final_state')}")
        gate(f"{name}_invalid_zero", s.get("invalid", -1) == 0, f"invalid={s.get('invalid')}")
        gate(f"{name}_ran_full_duration", elapsed_s >= DURATION_MIN_FRACTION * duration_s,
             f"elapsed={elapsed_s:.1f} s, min={DURATION_MIN_FRACTION * duration_s:.1f} s")
        min_samples = RTT_SAMPLE_MIN_FRACTION * HEARTBEATS_PER_S * duration_s
        gate(f"{name}_rtt_sample_count", (s.get("rtt_samples") or 0) >= min_samples,
             f"samples={s.get('rtt_samples')}, min={min_samples:.0f}")
        p99 = s.get("rtt_p99_ms", -1)
        gate(f"{name}_rtt_p99", 0 <= p99 <= rtt_limit,
             f"p99={p99} ms, limit={rtt_limit} ms (configured {configured_rtt} + {RTT_SCHEDULING_ALLOWANCE_MS}; synthetic loopback bound)")
        gate(f"{name}_download_budget", bool(s) and kin <= down_budget, f"in={kin} kbit/s, budget={down_budget:.2f}")
        gate(f"{name}_upload_budget", bool(s) and kout <= up_budget, f"out={kout} kbit/s, budget={up_budget:.2f}")
        mc = s.get("max_correction")
        gate(f"{name}_correction_budget", mc is not None and mc <= corr_budget, f"max_correction={mc}, budget={corr_budget:.3f}")
        if is_stalled:
            gate(f"{name}_reconnected_once", s.get("reconnects") == 1, f"reconnects={s.get('reconnects')}")
            gate(f"{name}_recovery_time", rec is not None and rec <= RECOVERY_AFTER_STALL_MAX_MS,
                 f"recovery={rec} ms, limit={RECOVERY_AFTER_STALL_MAX_MS} ms")
        else:
            gate(f"{name}_no_reconnects", s.get("reconnects") == 0, f"reconnects={s.get('reconnects')}")

    if run.get("stall_s", 0) * 1000 > run.get("host_timeout_ms", 3000) and stalled:
        # A stall longer than the host timeout must be seen as a timeout and recovered via the token (same identity).
        gate("host_timeouts_match_stalls", hs.get("timeouts") == len(stalled), f"timeouts={hs.get('timeouts')}")
        gate("host_reconnects_match_stalls", hs.get("reconnects") == len(stalled), f"reconnects={hs.get('reconnects')}")
    gate("host_joins_match_manifest", hs.get("joins") == len(expected), f"joins={hs.get('joins')}, expected={len(expected)}")

    return {
        "gate_version": GATE_VERSION,
        "clock": "real-time (wall clock, separate processes, loopback)",
        "run": run,
        "budgets": {"rtt_p99_ms": rtt_limit, "download_kbit_s": round(down_budget, 2), "upload_kbit_s": round(up_budget, 2),
                    "max_correction": round(corr_budget, 3)},
        "host": {k: hs.get(k) for k in ("world_tick", "uptime_us", "skipped_ticks", "shed_inputs", "stale_inputs", "joins",
                                        "reconnects", "timeouts", "timeout_notices", "invalid", "unknown_peer",
                                        "bytes_in", "bytes_out")},
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
    out = run_dir / "summary.json"
    if out.exists():
        print(f"error: {out} already exists; soak evidence is immutable (analyze into a fresh run directory)", file=sys.stderr)
        return 2
    try:
        result = analyze(run_dir)
    except (OSError, KeyError, json.JSONDecodeError) as e:
        print(f"error: cannot analyze {run_dir}: {e}", file=sys.stderr)
        return 2
    out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for g in result["gates"]:
        print(f"[{'PASS' if g['passed'] else 'FAIL'}] {g['gate']}: {g['detail']}")
    for name, c in result["clients"].items():
        print(f"  {name}: rtt p50/p95/p99/max = {c['rtt_p50_ms']}/{c['rtt_p95_ms']}/{c['rtt_p99_ms']}/{c['rtt_max_ms']} ms, "
              f"corrections={c['corrections']} (p99 {c['correction_p99']}, max {c['max_correction']}), stale={c['stale']}, "
              f"in/out={c['kbit_s_in']}/{c['kbit_s_out']} kbit/s, recovery={c['recovery_after_stall_ms']} ms")
    print(f"  host: drift={result['host_tick_drift']}, shed_inputs={result['host']['shed_inputs']}, "
          f"timeouts={result['host']['timeouts']}, reconnects={result['host']['reconnects']}")
    print(f"SOAK {'PASSED' if result['passed'] else 'FAILED'} (gate version {GATE_VERSION})")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
