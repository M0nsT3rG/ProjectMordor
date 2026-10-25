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


def main() -> int:
    rng = random.Random(36)
    samples = [establish_ms(rng) for _ in range(200_000)]
    out = {"model": "4-leg / 500 ms retry (F-GNS-003)", "attempt_success_p": round((1 - LOSS) ** 4, 4), **percentiles(samples),
           "exp037_proportions_without_with_duplicates": proportions()}
    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
