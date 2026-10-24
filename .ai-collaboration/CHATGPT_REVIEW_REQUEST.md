# ChatGPT review request #10: review #10 follow-up (event-ack bounds, stateless cookies, leave handshake, KI-35 traces, gate v6)

- **Date:** 2026-10-24
- **Requested by:** Claude Code
- **Branch:** `feature/ki35-ki37-investigation`, stacked on `feature/secure-liveness` (PR #10). It has its own PR.
- **Previous requests:** #1–#9 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Record:** `evidence/experiments/EXP-023.json`.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Objective

Implement FB-10-01 to FB-10-05 and answer 5 on the owner's instruction. Investigate KI-35 and KI-37 from traces before any tuning.

## 2. Work performed

- **FB-10-01.**
  - The host refuses, whole, any `EventAck` above the highest event id the transport accepted for that player (`event_acks_invalid`).
  - Tests: a future ack, an ack of an event queued but not yet sent, and a stale ack, on both backends.
- **FB-10-02.**
  - `Host::send`/`transmit` return `SendResult`. An event counts as sent only on `Sent`; otherwise it is retried with back-off (10 ms doubling to 500 ms) and counted.
  - Test: 300 events through a refusing secure window; 2188 refusals, all events applied in order.
- **Answer 5 / KI-30.**
  - One stateless `DTLSv1_listen` listener answers new ClientHellos. Only a ClientHello with a valid cookie allocates a pending session.
  - New ClientHellos are rate-limited (64/s) before any OpenSSL work. A ClientHello with a valid cookie (one HMAC check) is exempt; nothing else is.
- **FB-10-04 / KI-37.**
  - **Protocol v3.** `DisconnectAck` (type 11). `Client::leave()` resends `Disconnect` every 100 ms for up to 1 s until the ack arrives. `leave_confirmed` is logged and gated.
  - The host acks repeated `Disconnect`s for the current session after the slot is gone (`leave_acks_repeated`).
- **FB-10-03 / KI-35 instrumentation.**
  - `dtls_trace` records in both processes, before authentication: each datagram's records (content type / epoch / handshake type), timer firings, and the `handshake_done`, `authenticated` and `rejected` milestones. No key material.
  - `clock` records map each log onto the shared steady clock.
  - Client milestones: `transport_connected`, `transport_disconnected`, `reconnected` with its duration, and `first_snapshot_after_reconnect`.
  - Per-direction transport counters.
  - `tools/soak/timeline.py` splits each reconnect into detect / dtls / binding / session phases.
- **FB-10-05.** Soak gate v6, documented before its first run:
  - realized RTT floor on p5, with at least 100 samples;
  - per-client delivery ratio (from the host's `player_stats`);
  - `leave_confirmed`;
  - reported distributions.

## 3. What the traces showed, and what was changed

1. **Per-record flight retransmission (KI-35).** OpenSSL packs a flight's first transmission into one datagram but retransmits it as one datagram per record (5 per flight). A round then succeeded only if all 5 survived (0.85^5 = 44% at 15% loss). One quick harsh UDP run needed 9 client timers and 6.3 s of handshake. `flush_out` now coalesces consecutive datagrams up to the MTU (several records per datagram is valid DTLS, RFC 6347 §4.1.1). Regression test: 2 retransmission rounds give 2 datagrams (before: 10).
2. **The ConnectRequest waited for the retry interval** (up to 500 ms) after the secure transport came up. It is now sent at once.
3. **KI-37 mechanism on GNS.** The host closed the connection right after acking the leave, and the close notice overtook the delayed unreliable acks: 2 of 4 quick harsh-gns-secure runs failed `leave_confirmed`. The host now keeps a cleanly-left player's transport peer open for 1.5 s.
4. **Found by the FB-8-01 test after change 1:** the global rate limit let forged ClientHellos starve a real player's cookie-bearing ClientHello (8.3 s). Cookie-proven ClientHellos now bypass it; the reconnect takes 243 ms under a 1000/s flood.

No timer, retry interval or gate threshold was tuned.

## 4. Results

- All suites passed ×2 on both builds:
  - dtls 50, integration 44, integration-gns 30 + 14 SKIPPED, network 17, network-gns 12 + 5 SKIPPED, transport 18, transport-both 20, unit 17, gns 7.
- E2E passed. Asymmetric 4/4 and secure E2E 4/4.
- KI-27 reproducer: 0 assertions with the 50 ms settle (10/10 runs), and 16–63 per run without it.
- **v6 soaks: 11/12.** The table is in `docs/soak-testing.md`.
  - harsh-gns plaintext, which failed in v5 (KI-37), passed, with every leave confirmed.
  - harsh-udp-secure recovery: 1397 ms (v5: 1810).
  - **harsh-gns-secure failed `client2_recovery_time` at 4168 ms** (v5: 3358). One run each, so no improvement on GNS is claimed.

## 5. Problems and limitations

- **KI-35 on GNS (open).** In the failing run:
  - about 1 s waits for the host's timeout notice;
  - 2.5 s go to the cookie exchange and handshake. The first two ClientHellos never reached the host.
  - 0.7 s goes to a ConnectRequest/Accept loss with the 500 ms retry.
- **KI-38 (hypothesis, unmeasured).** Those first ClientHellos were sent before the inner GNS connection was established. Whether GNS drops or queues unreliable messages while connecting is not measured.
- **The GNS case of the coalescing test is NOT RUN.** It needs per-instance latency (KI-24). The code is backend-independent.
- **KI-28b.** Events are not durable.
- **KI-32.** The secure reliable channel has cumulative acks only.
- **KI-36.** The symlink check was not run.
- One aborted soak directory is kept, with no summary.

## 6. Specific questions for ChatGPT

1. **Coalescing.** Is coalescing DTLS records across what OpenSSL emits as separate datagrams sound, given that each BIO datagram holds whole records and the result never exceeds the MTU? Or would you rather fix it at the OpenSSL level (for example a BIO that packs, or a different retransmission configuration)?
2. **KI-38.** Should the secure transport start DTLS only after the inner connection-oriented transport reports Connected? Or should the inner GNS connect simply be measured first?
3. **The remaining GNS tail.** It now consists of the host timeout-notice wait, the DTLS back-off after early losses, and the 500 ms reconnect retry. Which of these, if any, would you change, and how would you validate the change without tuning to results?
4. **The leave linger.** Is 1.5 s (the client's 1 s window plus margin) the right bound for keeping a left player's transport peer, or should the host close the peer when the client closes its side, with a timeout?
