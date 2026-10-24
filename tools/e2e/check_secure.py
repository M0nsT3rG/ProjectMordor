"""Gates for scripts/secure-e2e.ps1 (review #8 FB-8-03). Fixed before the first run.

Positive run (host + client1 + client2, all secure):
  - every process exits 0; host and both clients log security mode "dtls";
  - both clients end connected and applied > 50 snapshots;
  - the host authenticated both players and dropped no non-DTLS datagram; no host timeouts (clean, reliable leave).
Negative runs (second secure host inviting only client1):
  - plaintext, stranger key and wrong invitation clients exit 1 (could not connect);
  - expired invitation and missing key exit 2 (refused at startup) with the reason on stderr;
  - the second host authenticated nobody and dropped the plaintext client's datagrams.
Secrets: no log contains a key-file header or the temporary key directory name.
Writes check.json; refuses to overwrite an existing one.
Usage: python -I tools/e2e/check_secure.py <run_dir>
"""
import json
import sys
from pathlib import Path


def events(path: Path):
    out = []
    if not path.exists():
        return out
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            out.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return out


def last(evts, name):
    found = [e for e in evts if e.get("evt") == name]
    return found[-1] if found else {}


def check(run_dir: Path) -> dict:
    run = json.loads((run_dir / "run.json").read_text(encoding="utf-8-sig"))
    gates = []

    def gate(name, ok, detail=None):
        gates.append({"gate": name, "passed": bool(ok), "detail": detail})

    for name, code in run["exit_codes"].items():
        gate(f"{name}_exit_0", code == 0, code)
    logs = {n: events(run_dir / f"{n}.log") for n in ("host", "client1", "client2", "host2")}
    for n in ("host", "client1", "client2", "host2"):
        gate(f"{n}_security_dtls", last(logs[n], "security").get("mode") == "dtls", last(logs[n], "security").get("mode"))
    for n in ("client1", "client2"):
        s = last(logs[n], "summary")
        gate(f"{n}_ends_connected", s.get("final_state") == "connected", s.get("final_state"))
        gate(f"{n}_snapshots", (s.get("snapshots") or 0) > 50, s.get("snapshots"))
    ss = last(logs["host"], "secure_stats")
    gate("host_authenticated_both", (ss.get("authenticated") or 0) >= 2, ss.get("authenticated"))
    gate("host_no_plaintext_datagrams", ss.get("dropped_not_dtls") == 0, ss.get("dropped_not_dtls"))
    gate("host_no_timeouts", last(logs["host"], "host_stopped").get("timeouts") == 0, last(logs["host"], "host_stopped").get("timeouts"))
    neg = run["negative_exit_codes"]
    for n in ("plaintext", "stranger", "wrong_invitation"):
        gate(f"negative_{n}_cannot_connect", neg.get(n) == 1, neg.get(n))
    for n, reason in (("expired_invitation", "expired"), ("missing_key", "key file not found")):
        err = (run_dir / f"{n}.stderr.txt").read_text(encoding="utf-8", errors="replace") if (run_dir / f"{n}.stderr.txt").exists() else ""
        gate(f"negative_{n}_refused_at_startup", neg.get(n) == 2 and reason in err, [neg.get(n), err.strip()[:120]])
    ss2 = last(logs["host2"], "secure_stats")
    gate("host2_authenticated_nobody", ss2.get("authenticated") == 0, ss2.get("authenticated"))
    gate("host2_dropped_plaintext", (ss2.get("dropped_not_dtls") or 0) >= 1, ss2.get("dropped_not_dtls"))
    leaked = [p.name for p in run_dir.iterdir() if p.is_file() and p.suffix in (".log", ".txt")
              and any(s in p.read_text(encoding="utf-8", errors="replace") for s in ("MRDRKEY1", "mordor-secure-e2e-"))]
    gate("no_key_material_or_key_paths_in_logs", not leaked, leaked)
    return {"transport": run["transport"], "passed": all(g["passed"] for g in gates), "gates": gates}


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
    print(("SECURE E2E PASSED" if result["passed"] else "SECURE E2E FAILED") + f" ({result['transport']})")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
