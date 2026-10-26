"""KI-35 / EXP-036: Monte Carlo of GNS direct-UDP connection establishment under our harsh fake impairment.

Model (F-GNS-003): the client starts an attempt every 500 ms (k_usecConnectRetryInterval). An attempt is 4 one-way
packets in sequence (ChallengeRequest, ChallengeReply, ConnectRequest, ConnectOK) and succeeds only if all 4 survive.
One-way delay follows our GNS mapping of the harsh profile (src/network/gns_transport.cpp): lag 80 ms, jitter with
average 10 ms capped at 20 ms (modelled as an exponential capped at the max), and a 20 ms reorder delay on 20% of
packets. Loss 15% per packet. Duplicates (5%) are NOT modelled (they could only make reality faster).
  python -I tools/ki35/model.py            # prints p50 / p90 / p99 in ms (fixed seed)
"""
from __future__ import annotations

import json
import random
import sys

RETRY_MS = 500.0
LOSS = 0.15
LAG_MS, JITTER_AVG_MS, JITTER_MAX_MS = 80.0, 10.0, 20.0
REORDER_PCT, REORDER_MS = 0.20, 20.0


def one_way(rng: random.Random) -> float:
    d = LAG_MS + min(rng.expovariate(1.0 / JITTER_AVG_MS), JITTER_MAX_MS)
    return d + (REORDER_MS if rng.random() < REORDER_PCT else 0.0)


def establish_ms(rng: random.Random) -> float:
    k = 0
    while True:
        if all(rng.random() >= LOSS for _ in range(4)):
            return k * RETRY_MS + sum(one_way(rng) for _ in range(4))
        k += 1


def percentiles(samples: list[float], ps=(50, 90, 99)) -> dict[str, float]:
    s = sorted(samples)
    return {f"p{p}": round(s[min(len(s) - 1, int(p / 100 * len(s)))], 1) for p in ps}


DUPLICATE = 0.05


def leg(with_duplicates: bool) -> float:
    """P(one packet gets through). With duplicates, assumed independent: lost only if it and its copy (if any) are lost."""
    return 1 - LOSS * (1 - DUPLICATE * (1 - LOSS)) if with_duplicates else 1 - LOSS


def proportions() -> dict[str, list[float]]:
    """EXP-037 (review #25 FB-25-01): per-attempt success proportions, [without, with] duplicates.
    gns_first_cycle: the 4-packet GNS exchange succeeds on its first 500 ms cycle (F-GNS-003).
    dtls_no_timer:   the 6 handshake flights with the cookie (CH, HVR, CH+cookie, server flight, client flight, server
                     Finished; each flight one datagram, coalesced) all arrive, so the client's retransmission timer
                     never fires (its first timeout, 250 ms, exceeds one RTT).
    session_first:   one ConnectRequest suffices: the client's binding (the host drops records before it authenticates),
                     the ConnectRequest and the ConnectAccept all arrive (3 packets). session_first_2leg: the same
                     without the binding condition, as the alternative."""
    out = {}
    for name, legs in (("gns_first_cycle", 4), ("dtls_no_timer", 6), ("session_first", 3), ("session_first_2leg", 2)):
        out[name] = [round(leg(False) ** legs, 4), round(leg(True) ** legs, 4)]
    return out


def udp_copies(rng: random.Random) -> list[float]:
    """Arrival delays (ms) of one datagram through our UDP impairment (src/network/impairment.cpp, harsh): lost whole
    with p = LOSS, else 1 copy (2 with p = DUPLICATE), each copy delayed 80 ms + U[0, 40] ms independently."""
    if rng.random() < LOSS:
        return []
    return [80.0 + rng.uniform(0.0, 40.0) for _ in range(2 if rng.random() < DUPLICATE else 1)]


def reorder_model(samples: int = 1_000_000, seed: int = 42) -> dict[str, float]:
    """EXP-042: the client sends its binding and, right after it (same update, same send time), the first
    ConnectRequest. The host drops application records that arrive before the binding (app_dropped_before_auth); a
    tie goes to the binding (the impairment queue is stable). Each request copy the host processes gets its own
    ConnectAccept back through the same impairment.
      d1:            the host drops >= 1 copy of the first request
      d1_in_order:   the same if delivery kept send order (the binding is only missing when it is lost): the contrast
      first:         the first request completes the session (some copy processed and some accept arrives)
      first_given_d1 / first_given_not_d1: the same, conditional"""
    # Amended before any run (Codex review of the tooling): a duplicate is the same DTLS record, and OpenSSL's replay
    # window drops a record already read (tests/dtls/dtls_proof_tests.cpp, dtls12_replayed_and_tampered_records_are_
    # dropped). So only each record's EARLIEST copy matters: the request is dropped if it arrives before the binding
    # (or the binding never does), processed otherwise, and a processed request gets exactly one accept.
    rng = random.Random(seed)
    n = d1 = d1_order = first = first_d1 = n_d1 = first_nd1 = 0
    for _ in range(samples):
        b, r = udp_copies(rng), udp_copies(rng)
        bmin = min(b) if b else float("inf")
        dropped = bool(r) and min(r) < bmin
        processed = bool(r) and not dropped
        ok = processed and bool(udp_copies(rng))
        n += 1
        d1 += dropped
        d1_order += (not b) and bool(r)
        first += ok
        if dropped:
            n_d1 += 1
            first_d1 += ok
        else:
            first_nd1 += ok
    return {"d1": round(d1 / n, 4), "d1_in_order": round(d1_order / n, 4), "first": round(first / n, 4),
            "first_given_d1": round(first_d1 / n_d1, 4), "first_given_not_d1": round(first_nd1 / (n - n_d1), 4)}


def main() -> int:
    if sys.argv[1:] == ["--reorder"]:
        print(json.dumps(reorder_model()))
        return 0
    rng = random.Random(36)
    samples = [establish_ms(rng) for _ in range(200_000)]
    out = {"model": "4-leg / 500 ms retry (F-GNS-003)", "attempt_success_p": round((1 - LOSS) ** 4, 4), **percentiles(samples),
           "exp037_proportions_without_with_duplicates": proportions()}
    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
