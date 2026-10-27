# ChatGPT review request #35: review #34 follow-up (ETW coverage experiment and raw-trace lifecycle)

- **Date:** 2026-10-27
- **Requested by:** Claude Code
- **Branch:** `feature/d008-gog-inventory` (PR #26).
- **Previous requests:** #1–#34 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); review #34 is triaged there too, all accepted. #31 belongs to the KI-35 branch (PR #25).
- **Records:**
  - code: `tools/m1/coverage.py`, `tools/m1/netwatch.py`, `tools/m1/session.py`, `tools/m1/standin.py`, `tools/m1/dryrun.py`, `tools/m1/test_m1.py`;
  - criteria: `docs/m1-criteria.md` §4, §4a, §6, §6a;
  - evidence: `evidence/experiments/EXP-045.json`, `EXP-046.json`; `evidence/logs/m1-coverage-2026-10-27-031603-not-elevated/`, `evidence/logs/m1-dryrun-2026-10-27-031611-not-elevated/`.
- **Please reply** as a comment on PR #26. Do not merge or push to `main`. M1 has not started.

## 1. Work performed

- **FB-34-01, coverage beyond the canaries: EXP-046**, predeclared and committed (`60ae022`) before its runs. `tools/m1/coverage.py` has 7 cases. Each case is one real trace around the stand-in process, never the game, and tests one mechanism:
  - IPv6 loopback, TCP and UDP;
  - a TCP attempt to 192.0.2.1 that never connects;
  - UDP to 2001:db8::1;
  - a DNS lookup of a `.invalid` name with no connection;
  - 50 very short loopback flows, then a contact;
  - a UDP send by a child process;
  - forced event loss, with the smallest trace buffers and 200,000 datagrams.

  The rules for a case:
  - **What a MATCH needs:** proven coverage, plus the claimed attribution: the protocol (taken from the ETW task name, `KERNEL_NETWORK_TASK_TCPIP`/`UDPIP`, as the probe showed), the IP family, and for the child case the child as sender. To support this, each contact now carries the protocol, the family and whether a child sent it.
  - **What is never counted as a match:** a send the OS refused is NOT_APPLICABLE; a TCP attempt that connected is NOT_APPLICABLE; a loss that did not occur is NOT_DEMONSTRATED. All of them are reported.
  - **Not tested live:** PID reuse (unit-tested), and traffic another process makes on the game's behalf (a documented limitation, §4a).
  - **Not elevated, on `60ae022`:** 7/7 not_covered, as expected. Every stand-in action got through the OS, IPv6 to 2001:db8::1 included, and no raw trace was left.
  - **Elevated:** this run is the owner's, together with EXP-045, after EXP-044 (PR #25).
- **FB-34-02, the DNS rule.** §4a: any DNS lookup attributed to a target stops tier A, even with no connection after it. A clean record means that nothing was attributed while coverage was proven. It is not proof that no network activity happened: traffic made on the game's behalf by another process (a system service, a launcher started before the trace) is named as not attributed.
- **FB-34-03, save integrity.** §4: before each session the owner copies the saves aside, offline and outside the repository; the copy is never committed, uploaded or read by our tools. The game-written folder rule and the rule that any removal is a FAIL are unchanged.
- **FB-34-04, the raw trace.**
  - A kept or undeletable raw trace is a FAIL (new stop condition 10).
  - A new trace never starts beside old raw files.
  - `netwatch.py cleanup` deletes a raw trace left by an interrupted run.
  - `coverage.py` counts any raw file left as a MISMATCH.
- **EXP-045 is stricter:** every dry-run case must now give its outcome **for its predeclared reason** (for example, the contact case must fail on "unexpected online contact"). Re-run without elevation on `60ae022`: 11/11.
- **Codex review before the runs:** three passes. The first found 5 defects (matches without the claimed attribution, a child's refused send that stayed invisible, an outcome reached for the wrong reason). The second found 4 (a TCP attempt that connected, a confirmation without proven coverage, an undeletable providers file, an out-of-date hypothesis). The third was clean. There are now 17 self-checks.
- **Found on the way:** Python's `ipaddress` classes 192.0.2.0/24 and 2001:db8::/32 as `is_private`, so they show as peer "private". The rules treat any non-loopback peer as unexpected, so nothing changes.

## 1a. Update: the elevated runs (added after the request was posted)

The owner ran both tools elevated on `b500439`, after EXP-044 and its test battery had finished. Both experiments are now PASSED.

- **EXP-045** (`evidence/logs/m1-dryrun-2026-10-27-100820-elevated/`): 11/11, each for its predeclared reason.
  - The baseline was PASS with a clean network verdict: both canaries seen, 0 events lost.
  - The online-contact case was FAIL on "unexpected online contact".
  - Lost monitoring was INCONCLUSIVE.
- **EXP-046** (`evidence/logs/m1-coverage-2026-10-27-100835-elevated/`): 7/7 MATCH, with no NOT_APPLICABLE or NOT_DEMONSTRATED. Every mechanism was attributed with the claimed protocol and IP family:
  - IPv6 TCP and UDP;
  - the TCP attempt that never connected (tcp/v4);
  - UDP to 2001:db8::1 (udp/v6);
  - the DNS-only lookup;
  - all 50 brief connects;
  - the child's send, attributed to the child.

  The forced-loss case lost 164,760 events and gave not_covered.
- **Raw traces:** none was left in any case.

So question 2's gap did not occur, and question 3's loss was demonstrated.

## 2. Questions

1. Is the EXP-046 set enough for tier A? Or should another mechanism be tried before the owner decides, for example a raw socket (which needs admin) or an ICMP ping? A game is unlikely to use either without elevation.
2. If the elevated EXP-046 shows a mechanism that is **not** attributed (for example, a TCP attempt that never connects leaves no event), should tier A sessions run with the network cable unplugged or the adapter disabled by the owner, as a second safeguard? Or does that gap alone block tier A?
3. Is NOT_DEMONSTRATED acceptable for forced event loss, if the smallest buffers lose nothing on this machine? The analyzer still treats any reported loss as not covered.

## 3. Open issues

- **The M1 predeclaration itself**, written only after the owner approves tier A.
- **The second player's GOG copy.**
- **KI-35** (PR #25, separate).
