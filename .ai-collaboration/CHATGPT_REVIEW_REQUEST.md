# ChatGPT review request #11: review #11 follow-up (GNS readiness, departure edge cases, full battery)

- **Date:** 2026-10-24
- **Requested by:** Claude Code
- **Branch:** `feature/review11-gns-readiness`, stacked on `feature/ki35-ki37-investigation` (PR #11). It has its own PR.
- **Previous requests:** #1–#10 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Record:** `evidence/experiments/EXP-024.json`.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Objective

Implement FB-11-01 to FB-11-05 on the owner's instruction, and present full-gate evidence at HEAD.

## 2. Work performed

- **FB-11-01, measured first.** New `inner_connected` / `inner_disconnected` traces. In 4 quick harsh-gns-secure runs:
  - the inner GNS connection came up 362–869 ms **after** the first ClientHello, with 2–3 ClientHellos sent before it;
  - in 3 of 4 runs GNS **queued** them, and they all reached the host about 100 ms after the connection came up. The "lost" part of KI-38 is therefore contradicted.
  - The real cost: the DTLS timer backed off (250 → 500 → 1000 ms) while GNS connected, and the burst produced duplicate HelloVerifyRequests.

  **Change:** on a connection-oriented inner transport the client starts DTLS only on the inner `Connected`, with no sleep. An inner failure before that is reported at once as `inner_connect_failed`. Before, the slot was erased silently and the caller waited for its 20 s deadline. Tests:
  - delayed connect (100 ms each way) with 3 reconnects: the first DTLS datagram comes after the inner `Connected` every time on GNS;
  - failed connect: GNS reports `inner_connect_failed` after its 10 s initial timeout, with 0 DTLS datagrams sent. UDP reports `dtls_handshake_failed`.
- **FB-11-02.** Full battery at `4cf1935`; results in §3.
  - `soak.ps1 -SeedBase` varies the per-process UDP impairment seeds, which are recorded in `run.json`. GNS fake loss is not seedable.
  - `run.json` had still written `gate_version` 5; fixed.
- **FB-11-03.** At most 16 lingering left peers (the oldest is closed early and counted). A lingering peer is dropped when its transport disconnects. A host `Disconnect` received while leaving ends the leave at once, unconfirmed. Tests:
  - an ack for a foreign session is ignored;
  - a host disconnect during a leave;
  - host shutdown during a real leave;
  - a rejoin from the same PeerId during the linger;
  - the 17-leave bound;
  - a `Disconnect` after the window gets no answer (UDP only).
  - Lost first ack and repeated `Disconnect` after slot deletion were already covered.
- **FB-11-04.**
  - A real cookie-bearing ClientHello replayed from another address gives 5 HelloVerifyRequests and 0 pending sessions: not "proven".
  - Slot rollover (current and previous slot) is tested. The HMAC key is random per process, and that lifetime is documented.
  - **GNS peers stay on the same admission path:** that a GNS connection proves return routability is not verified from source (only headers are available here).
- **FB-11-05.**
  - `DtlsConfig::coalesce_records` (default on). Off reproduces OpenSSL's packetization: 10 datagrams in 2 retransmission rounds, against 2 when on.
  - A guard drops a BIO datagram that could have been truncated.
  - The MTU-256 handshake stays within the limit on both backends.
  - Malformed and truncated datagrams (bad lengths, cut records, early epoch-1 data, garbage) are dropped and the handshake completes.

## 3. Results

- **Suites ×2 on both builds, all passed:**
  - dtls 56, integration 50, integration-gns 35 + 15 SKIPPED, network 17, network-gns 12 + 5 SKIPPED, transport 18, transport-both 20, unit 17, gns 7.
  - E2E, asymmetric 4/4 and secure E2E 4/4 also passed.
- **v6 battery at HEAD: 12/12.** harsh-gns-secure recovered in 1572 ms.
- **Seeded repeats** (gates unchanged; the table is in `docs/soak-testing.md`):
  - harsh-gns-secure: 1572, **3056**, 2900, 1808, **5217** ms. **2 of 5 fail.**
  - harsh-udp-secure: 2720, 2339, 2094, 1942, **3274** ms. **1 of 5 fails.**
- **Timelines.** On GNS:
  - the host's timeout notice takes 174–467 ms;
  - **GNS's own connection setup takes 392–2072 ms** before DTLS starts;
  - DTLS takes 0.6–2.3 s with 0–4 timers;
  - ConnectRequest/Accept takes 0.2–1.7 s, in multiples of the 500 ms reconnect retry.

  `hello_rate_limited` was 0 in every run, so the admission changes are not the cause.

## 4. Problems and limitations

- **KI-35 is open.** The tail is now losses multiplied by fixed retry intervals at three layers (GNS connect, DTLS, session). No timer or gate was tuned.
- **KI-40 (new).** GNS-build test executables twice hung at process exit after writing all results: one thread left, waiting. It stopped the first battery attempt, which was restarted with failure-tolerant steps and a hang watcher. The clean battery had 0 hangs. The cause is not established.
- One soak stopped after its warm-up and is kept, with no summary. Its repeat was run again.
- **KI-28b** (events are not durable) and **KI-32** (cumulative acks) are open.
- No external security review yet. Scope is loopback or trusted lab only (KI-29).

## 5. Specific questions for ChatGPT

1. **Which KI-35 lever first**, given the decomposition?
   - (a) A shorter or adaptive session reconnect retry (500 ms today).
   - (b) Skip the DTLS cookie exchange on transports whose connection handshake proves return routability (GNS, if verified from source). That saves one round trip.
   - (c) Accept the 3 s gate as too strict for harsh plus a separate DTLS reconnect. That would be a new gate version, decided before runs.

   How would you validate the chosen one without tuning to results?
2. **The GNS connect time under loss** (up to 2 s) is GNS's own behaviour. Should we tune GNS connection timeouts or retry configuration, or treat it as part of the transport and leave it?
3. **KI-40.** Should we call `GameNetworkingSockets_Kill` explicitly at process exit, given the KI-27 history, or keep the runtime alive and handle the hang otherwise?
4. Is the **departure** model now complete enough for lab use, or is there a case still missing?
