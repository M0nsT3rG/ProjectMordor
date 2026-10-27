"""Self-checks for the EXP-044 decision arithmetic: python -I tools/ki35/test_run_timer_probe.py"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_timer_probe as r  # noqa: E402


def cycle(j, arm, profile, connected=1500.0, outcome="ok", tx=(4, 4)):
    return {"cycle": j, "log_ms": j * 10_000.0, "outcome": outcome, "reason": "connect_accept", "notice": 100.0,
            "inner": 600.0, "first_hello": 600.0, "hvr": 800.0, "cookie_hello": 800.0, "auth": connected - 100.0,
            "connected": connected, "first_snapshot": connected + 50, "hellos": 2, "timers": 1, "loop_ms": 1.5,
            "host_auth": connected - 150.0, "host_drops": [], "gate_timeouts": 0, "arm": arm, "profile": profile,
            "host_timers": 1, "client_tx": tx[0], "host_tx": tx[1], "cpu_ms": 100.0 * (j + 1)}


def write_run(root: Path, slow_flat=10, slow_off=30, flat_tx=(4, 4), flat_failures=0, drop_batch="", drop_field="",
              bump_batch="", off_no_snapshot=0, no_reconnect_batches=()):
    codes = {}
    for b, s, a, p, n in r.BATCHES:
        nm = r.ab.name(b, s, a, p)
        if nm == drop_batch:
            continue
        d = root / nm
        d.mkdir()
        sf, so = (x[b] if isinstance(x, dict) else x for x in (slow_flat, slow_off))  # per backend, optionally
        slow = (sf if a == "flat" else so) if p == "harsh" else 0
        lines = []
        for j in range(n):
            last = p == "harsh" and j >= n - (flat_failures if a == "flat" else off_no_snapshot)
            outcome = ("no_reconnect" if nm in no_reconnect_batches else
                       ("timeout" if a == "flat" else "no_snapshot") if last else "ok")
            c = cycle(j, a, p, connected=4000.0 if j < slow else 1500.0, tx=flat_tx if a == "flat" else (4, 4),
                      outcome=outcome)
            if nm == bump_batch and j == 0:
                c["client_tx"] += 1
            if drop_field and j == 0:
                c.pop(drop_field)
            lines.append(json.dumps(c))
        (d / "cycles.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
        (d / "client.log").write_text("", encoding="utf-8")
        codes[nm] = 0
    (root / "run.json").write_text(json.dumps({"batches": r.BATCHES, "exit_codes": codes}), encoding="utf-8")


def judged(**kw):
    with tempfile.TemporaryDirectory() as tmp:
        write_run(Path(tmp), **kw)
        return r.judge(Path(tmp))


class Rules(unittest.TestCase):
    def test_the_batches_are_the_predeclared_ones(self):
        self.assertEqual(len(r.BATCHES), 16)
        self.assertEqual([a for *_, a, p, n in r.BATCHES[:4]], ["off", "flat", "off", "flat"])  # alternating
        self.assertEqual(sorted({s for b, s, a, p, n in r.BATCHES if b == "udp"}), [4401, 4402, 4403, 4404])

    def test_a_better_flat_arm_within_the_guards_means_growth_matters(self):
        v = judged(slow_flat=5, slow_off=40)
        self.assertTrue(v["complete"], v["incomplete_because"])
        self.assertEqual(v["rules"]["udp-harsh"]["O1_over_limit"]["verdict"], "flat_better")
        self.assertEqual(v["rules"]["reading"], "growth_matters")
        self.assertIn("O3_p50_dtls", v["rules"]["gns-latency"])

    def test_no_difference_means_the_cap_is_not_the_lever(self):
        v = judged(slow_flat=20, slow_off=20)
        self.assertEqual(v["rules"]["udp-harsh"]["O1_over_limit"]["verdict"], "no_difference_shown")
        self.assertEqual(v["rules"]["reading"], "timer_cap_not_the_lever")

    def test_a_worse_flat_arm_is_never_read_as_growth_matters(self):
        v = judged(slow_flat=40, slow_off=5)
        self.assertEqual(v["rules"]["udp-harsh"]["O1_over_limit"]["verdict"], "flat_worse")
        self.assertEqual(v["rules"]["reading"], "timer_cap_not_the_lever")

    def test_the_cost_guard_blocks_a_gain_bought_with_datagrams(self):
        v = judged(slow_flat=5, slow_off=40, flat_tx=(7, 6))  # 13 / 8 = 1.625 > 1.5
        self.assertEqual(v["rules"]["udp-harsh"]["C1_datagram_ratio"], 1.625)
        self.assertFalse(v["rules"]["guards"]["C1_cost"])
        self.assertEqual(v["rules"]["reading"], "timer_cap_not_the_lever")

    def test_the_robustness_guard_blocks_extra_timeouts(self):
        v = judged(slow_flat=5, slow_off=40, flat_failures=1)
        self.assertFalse(v["rules"]["guards"]["C2_robustness"])
        self.assertEqual(v["rules"]["reading"], "timer_cap_not_the_lever")

    def test_a_missing_batch_or_field_gives_no_reading(self):
        for kw in ({"drop_batch": "gns-2-flat-harsh"}, {"drop_field": "host_tx"}):
            v = judged(slow_flat=5, slow_off=40, **kw)
            self.assertFalse(v["complete"])
            self.assertEqual(v["rules"]["reading"], "incomplete")

    def test_an_empty_host_trace_is_incomplete_not_cheap(self):  # Codex passes 2 and 3
        for outcome in ("ok", "no_snapshot"):  # no_snapshot reached Connected too
            with tempfile.TemporaryDirectory() as tmp:
                write_run(Path(tmp), slow_flat=5, slow_off=40, flat_tx=(7, 6))
                p = Path(tmp) / "udp-4401-flat-harsh" / "cycles.jsonl"
                cs = [json.loads(l) for l in p.read_text(encoding="utf-8").splitlines()]
                for c in cs:
                    c.update(outcome=outcome, host_auth=-1.0, host_tx=0, host_timers=0)
                p.write_text("\n".join(json.dumps(c) for c in cs) + "\n", encoding="utf-8")
                v = r.judge(Path(tmp))
                self.assertFalse(v["complete"], outcome)
                self.assertEqual(v["rules"]["reading"], "incomplete")

    def test_a_timed_out_reconnect_needs_no_host_authentication(self):
        c = cycle(0, "flat", "harsh", connected=-1, outcome="timeout")
        c.update(auth=900.0, host_auth=-1.0)  # the client completed DTLS; the host never verified its binding
        self.assertTrue(r.traced(c))
        c.update(host_tx=0)  # but a completed client handshake means the host sent its flights
        self.assertFalse(r.traced(c))

    def test_every_recorded_phase_implies_its_datagrams(self):  # Codex pass 4
        base = cycle(0, "flat", "harsh", connected=-1, outcome="timeout")
        base.update(auth=-1.0, host_auth=1000.0)
        self.assertTrue(r.traced(base))
        for bad in ({"client_tx": 0, "host_tx": 0}, {"host_tx": 0}, {"client_tx": 1, "hellos": 2},
                    {"hellos": 0}, {"auth": -1.0, "host_auth": -1.0, "hvr": 800.0, "host_tx": 0}):
            self.assertFalse(r.traced({**base, **bad}), bad)
        with tempfile.TemporaryDirectory() as tmp:  # host-only authentication with zeroed counts: no reading
            write_run(Path(tmp), slow_flat=5, slow_off=40)
            p = Path(tmp) / "gns-1-flat-harsh" / "cycles.jsonl"
            cs = [json.loads(l) for l in p.read_text(encoding="utf-8").splitlines()]
            cs[-1].update(outcome="timeout", connected=-1.0, auth=-1.0, client_tx=0, host_tx=0)
            p.write_text("\n".join(json.dumps(c) for c in cs) + "\n", encoding="utf-8")
            self.assertEqual(r.judge(Path(tmp))["rules"]["reading"], "incomplete")

    def test_a_barely_worse_backend_is_not_hidden_by_rounding(self):  # Codex pass 5
        lo = r.ab.newcombe(159, 300, 135, 300, exact=True)[0]
        self.assertTrue(0 < lo < 0.00005)  # rounds to 0.0 at 4 places
        v = judged(slow_flat={"udp": 5, "gns": 53}, slow_off={"udp": 40, "gns": 45})
        self.assertEqual(v["rules"]["udp-harsh"]["O1_over_limit"]["verdict"], "flat_better")
        self.assertEqual(v["rules"]["gns-harsh"]["O1_over_limit"]["verdict"], "flat_worse")
        self.assertEqual(v["rules"]["reading"], "timer_cap_not_the_lever")

    def test_homogeneity_is_judged_before_rounding(self):  # Codex pass 6: p = 0.0099997 rounded to 0.01
        self.assertLess(r.rc.homogeneity_p([(21, 99), (26, 100), (40, 100)], exact=True), r.rc.HOMOGENEITY_P)
        with tempfile.TemporaryDirectory() as tmp:
            write_run(Path(tmp), slow_flat={"udp": 5, "gns": 20}, slow_off={"udp": 40, "gns": 20})  # GNS: no change
            for arm, slows in (("flat", (21, 26, 40)), ("off", (45, 45, 45))):
                for seed, slow in zip((4401, 4402, 4403), slows):
                    p = Path(tmp) / f"udp-{seed}-{arm}-harsh" / "cycles.jsonl"
                    cs = [json.loads(l) for l in p.read_text(encoding="utf-8").splitlines()]
                    for j, c in enumerate(cs):  # the first `slow` reconnects over 3 s
                        c.update(cycle(j, arm, "harsh", connected=4000.0 if j - (seed == 4401) < slow else 1500.0))
                    if seed == 4401:
                        cs[0]["outcome"] = "no_reconnect"  # 99 reconnects in that batch
                    p.write_text("\n".join(json.dumps(c) for c in cs) + "\n", encoding="utf-8")
            v = r.judge(Path(tmp))
            self.assertTrue(v["complete"], v["incomplete_because"])
            self.assertEqual(v["rules"]["udp-harsh"]["O1_over_limit"]["verdict"], "heterogeneous")
            self.assertEqual(v["rules"]["reading"], "timer_cap_not_the_lever")

    def test_the_cost_guard_is_exact_at_the_limit(self):  # Codex: a rounded 1.5004 passed
        self.assertTrue(judged(slow_flat=5, slow_off=40, flat_tx=(8, 4))["rules"]["guards"]["C1_cost"])  # 12/8 = 1.5
        v = judged(slow_flat=5, slow_off=40, flat_tx=(8, 4), bump_batch="udp-4401-flat-harsh")  # 3601/300 vs 2400/300
        self.assertFalse(v["rules"]["guards"]["C1_cost"])
        self.assertEqual(v["rules"]["reading"], "timer_cap_not_the_lever")

    def test_missing_snapshots_never_offset_extra_timeouts(self):  # Codex
        v = judged(slow_flat=5, slow_off=40, flat_failures=3, off_no_snapshot=3)
        self.assertFalse(v["rules"]["guards"]["C2_robustness"])
        self.assertEqual(v["rules"]["reading"], "timer_cap_not_the_lever")

    def test_unknown_homogeneity_is_never_a_favourable_reading(self):  # Codex
        empty = ("udp-4402-off-harsh", "udp-4403-off-harsh", "udp-4402-flat-harsh", "udp-4403-flat-harsh")
        v = judged(slow_flat=5, slow_off=40, no_reconnect_batches=empty)
        self.assertTrue(v["complete"])
        self.assertEqual(v["rules"]["udp-harsh"]["O1_over_limit"]["verdict"], "unassessable")
        self.assertEqual(v["rules"]["reading"], "unassessable")

    def test_cpu_counts_only_differences_within_a_batch(self):  # Codex: pooled batches are separate processes
        v = judged()
        self.assertEqual(v["batches"]["udp-4401-off-harsh"]["cpu_ms_per_cycle"], 100.0)
        self.assertEqual(v["pooled"]["udp-harsh"]["off"]["cpu_ms_per_cycle"], 100.0)
        s = v["pooled"]["udp-harsh"]["off"]
        self.assertEqual(s["datagrams_per_reconnect"], 8.0)
        self.assertEqual(s["timers_per_reconnect"], 2.0)


if __name__ == "__main__":
    unittest.main(verbosity=1)
