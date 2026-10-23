# ChatGPT feedback

This file holds feedback from ChatGPT, and Claude Code's triage of it.

Feedback is technical advice, **not authorisation**. Anything that conflicts with the project rules in `/CLAUDE.md` is rejected, and the reason is recorded. That covers game files and saves, DRM and anti-tamper, online services, and evidence integrity.

---

## Feedback on review #6 (PR #6: GNS hardening and DTLS proof)

- **Status:** triaged 2026-10-24 and **implemented** on `feature/dtls-transport` (stacked on PR #6), on the owner's go-ahead. Record: EXP-018. Review request #6.
- **As implemented:**
  - **FB-6-01:** fail-closed binding in `DtlsSession` (exporter confirmation over id, version and expiry). Tests cover each mismatched field, a missing confirmation and an expired invitation.
  - **FB-6-02:** DTLS over `Transport::Unreliable` on both backends with OpenSSL timers, plus loss/jitter/reorder/duplication, reconnect and MTU tests. GNS reliable delivery is not used (decided).
  - **FB-6-03:**
    - root cause localised by probe and source reading;
    - mitigated with a process-lifetime runtime and a 50 ms wait after the first Init;
    - regression gates added;
    - no upstream report filed.
  - **FB-6-04:** wording sweep (`transport.hpp`, `protocol.md`, KI-26).
  - **FB-6-05:** `scripts/asymmetric.ps1` with separate processes, passed on both transports.
  - **Answer 3:** `--connect-deadline-s` bounded to [1, 120] s.
  - **Answer 5:** HelloVerifyRequest cookies on both backends; statelessness is not done (KI-29).
- **Source:** [PR #6 comment 6376091101](https://github.com/M0nsT3rG/ProjectMordor/pull/6#issuecomment-6376091101). ChatGPT reviewed the source of `1fc796f` only.
- **ChatGPT's verdict:** conditional GO for isolated testing. Not approval for untrusted networks or for merge.

| ID | Assumptions verified? | Decision | Plan |
|---|---|---|---|
| **FB-6-01** (major) session binding | **Yes.** `dtls12_exporter_binds_invitation_and_is_fresh_per_handshake` only shows that the exporter values match when both sides choose the same string. Nothing exchanges or verifies the value, so peers holding different invitations still complete DTLS. The design (`authentication-design.md` §4a, "key confirmation… verified on both ends") is not implemented. | **Accepted** | A binding message inside the channel: each side sends a confirmation of the exporter over (invitation id, protocol version, expiry). It fails closed on mismatch, expiry or absence before any game message. Tests deliberately mismatch the id, the version and the expiry, and cover a missing confirmation. |
| **FB-6-02** (major) DTLS reliability evidence | **Yes.** The in-memory shuttle never loses, delays or reorders a datagram, so `DTLSv1_get_timeout`/`DTLSv1_handle_timeout`, retransmission and MTU fragmentation are not exercised. | **Accepted** | A DTLS layer over `Transport::Unreliable` on **both** backends, driven by OpenSSL's own timers (answer 4). Tests: mutual identity, handshake under loss, jitter and reorder, replay, reconnect = new handshake. GNS reliable delivery is **not** used to carry DTLS (decided and documented, as asked). |
| **FB-6-03** (major) KI-27 GNS assertion | **Yes.** It is a sanity check in `PollRawUDPSockets` (`steamnetworkingsockets_socketthread.cpp:2605–2609`): `Assert(pSock->m_callback.m_fnCallback)` over every raw socket. `CRawUDPSocketImpl::Close` sets the callback to null and **defers** the deletion (`ProcessPendingDestroyClosedRawUDPSockets`), so the assert fires if the service thread polls a closed socket before its cleanup runs. `DBGFLAG_ASSERT` is defined "even in release" (`tier0/dbg.h`), which is why we see it. **Hypothesis from the source, not proven:** a GNS-internal teardown-ordering race, triggered by our tests closing sockets while the service thread runs. | **Accepted.** It is not dismissed. | Capture every occurrence with the test name. Reproduce in isolation (an open/close loop), check whether our call pattern (closing while connections linger, `Kill` right after) is the trigger, and try a mitigation such as flushing callbacks before closing. Release suitability is gated on a root cause or a documented upstream cause, with a regression test. |
| **FB-6-04** (minor) guarantee wording | **Yes.** | **Accepted** | Sweep the docs and logs so that `Sent` is never described as delivery. KI-28 stays explicit. |
| **FB-6-05** (minor) coverage gaps | **Yes.** The 14 + 5 skips stay visible in `docs/test-coverage.md`. | **Accepted** | Asymmetric impairment on GNS with separate processes: an E2E/soak-style scenario where only one client process is impaired. |

**Answers received, and what follows from them:**
1. Sharing an identical config is fine if refcount and reset are tested (they are).
2. The counters are fine if explicitly labelled.
3. Make the 20 s deadline a configurable policy with an explicit maximum, plus tests: CLI `--connect-deadline-s`, clamped, with a test.
4. Run DTLS over the unreliable datagram transport with OpenSSL's own retransmission (matches FB-6-02).
5. Use stateless cookies (`DTLSv1_listen`) on raw UDP listeners, and do not treat GNS's handshake as a DoS or identity check either.
6. Investigate the assertion (FB-6-03).

## Feedback on review #5 (PR #5: GNS adapter)

- **Status:** triaged 2026-10-23. **Implemented** 2026-10-24 on `feature/gns-hardening` (stacked on PR #5), on the owner's go-ahead, which also approved the FB-5-05 proof test. Record: EXP-017. Review request #5.
- **As implemented:**
  - **FB-5-01:**
    - the counter semantics are fixed in `transport.hpp`; GNS `close()` counts connections in `closed_with_unacked_reliable`;
    - test `contract_mid_backlog_disconnect_then_reconnect_is_clean`;
    - application-level delivery is deferred to the reliable-events milestone (KI-28).
  - **FB-5-02:**
    - `connect_deadline_us` at the session layer, with a timeout table in D-014;
    - 4 lifecycle tests.
  - **FB-5-03:**
    - the guard (refuse a different config, join the same one, last holder resets) and `impairment_refused`;
    - test `contract_process_global_impairment_is_guarded_and_never_leaks`.
    - **It exposed a false GNS pass:** `timeout_notice_recovery_under_jitter_reconnects_once`, now UDP-only.
  - **FB-5-04:**
    - 8 `_over_transport` substitutes;
    - `docs/test-coverage.md`.
  - **FB-5-05:**
    - `tests/dtls`, 8/8 after a failed first run (DANE must be enabled);
    - see `docs/d013-evaluation.md`.
- **Source:** [PR #5 comment 6375464043](https://github.com/M0nsT3rG/ProjectMordor/pull/5#issuecomment-6375464043). ChatGPT reviewed the source of `9ae2b2a` only.
- **ChatGPT's verdict:** conditional GO for isolated lab work. Not production-ready, and no untrusted access.

| ID | Assumptions verified? | Decision | Plan | Verification planned |
|---|---|---|---|---|
| **FB-5-01** (major) reliability accounting | **Yes, confirmed in the code.** `Conn::reliable_outstanding` is one boolean per connection, cleared when the aggregate `m_cbPendingReliable`/`m_cbSentUnackedReliable` reach 0. It is wire-level only and says nothing about the peer application. GNS `discarded_on_close` counts **connections** (`gns_transport.cpp:165`), while UDP counts **messages**, so the same counter has two meanings. Also: if the last instance closes, `release()` calls `GameNetworkingSockets_Kill()`, which can end lingering connections early. | **Accepted** | (a) Document the transport counters as wire-level diagnostics, not proof of delivery. Give GNS a separate per-connection counter so `discarded_on_close` keeps one meaning. (b) Application-level delivery (event ids, app ACK, resume or snapshot replay) becomes part of the reliable-events milestone: there are no consequential events yet. (c) Add disconnect-mid-backlog and reconnect tests. | Contract tests for the counter semantics; mid-backlog tests on GNS. |
| **FB-5-02** (major) lifecycle and timeouts | **Partly confirmed.** The initial connect is bounded only by GNS's `TimeoutInitial` (10 s) plus our 5 s window; our own timer is skipped until transport `Connected` (`client.cpp:113`). If GNS never reported `Disconnected`, nothing would bound it: there is **no overall session deadline**. Reconnect is bounded by the 15 s window. No test covers a black-holed connect, no listener, or a close during the handshake. | **Accepted** | Add an overall connect deadline at the session layer that covers transport and session attempts. Document which layer owns each timeout. Add tests: no listener, black-holed connect, remote close during handshake, repeated reconnects, host restart (the last already exists). | New tests on both backends. |
| **FB-5-03** (major) process-global impairment | **Yes, confirmed.** Any instance's `set_impairment` overwrites the global config and takes ownership (`gns_transport.cpp:353–357`). An owner's `close()` resets impairment for every other instance. The single-owner rule is not enforced. | **Accepted** | Enforce a single owner: a second impairing instance gets an explicit error, never a silent overwrite. Add a leakage test. Run asymmetric GNS scenarios in **separate processes** (answer 6). | Leakage and guard tests; separate-process asymmetric test. |
| **FB-5-04** (minor) test completeness | **Partly.** Each of the 18 skips already states its reason in the code, and our reports gave skip counts. But 11 skips use a raw UDP fake peer, and several of them (malformed messages, foreign session, timeout-notice replay) test **our** protocol checks, which could run over a GNS connection carrying bad payloads. There is no coverage matrix. | **Accepted** | A coverage matrix (`docs/`) per test × backend, naming the capability behind each skip. GNS substitutes that send malformed or replayed protocol payloads through a real GNS connection. | Matrix plus new GNS tests. |
| **FB-5-05** (minor) D-013 | **Yes.** Our evaluation already required proof tests (condition 2), but it rests on header availability only. | **Accepted** | A minimal two-peer DTLS 1.2 + RFC 7250 proof test covering a correct pinned key, a wrong key, session binding and downgrade rejection. It links OpenSSL directly, so **it needs the owner's approval**: no new crypto dependency without it. | The proof test, if approved. |

**Answers received:**
1. Test changes are acceptable only where a backend capability differs, with a substitute invariant.
2. The counters are diagnostics; use app ACK and replay for events.
3. The 500 ms drain is a policy, not a guarantee.
4. Keep an overall deadline.
5. DTLS + RPK is plausible but unproven.
6. Use separate processes.

## Recommendation after review #4 (GNS adapter, priorities 1–3)

- **Status:** implemented on `feature/gns-adapter` (stacked on PR #4), 2026-10-23. Experiment record: EXP-016. Review request: [CHATGPT_REVIEW_REQUEST.md](CHATGPT_REVIEW_REQUEST.md) #4.

| Item | Decision | Implemented in | Verification |
|---|---|---|---|
| **P1** GNS adapter behind `Transport` | **Accepted** | `src/network/gns_transport.*`; `--transport gns`. ReliableOrdered and Unreliable; `PeerId` per connection; Connected/Disconnected events; reconnect = new connection (D-014); `LimitExceeded` → `Backpressure`; GNS-native impairment; host logs transport counters. | Contract suite 15/15 on both backends. |
| **P1** no silent drop of accepted reliable data | **Accepted** | The adapter tracks unacknowledged reliable data itself (GNS stops reporting it after a failure): `accepted_then_failed`, `closed_with_unacked_reliable`; bounded 500 ms drain on `close()`. | `contract_disconnect_with_pending_reliable_is_reported`, `contract_close_peer_with_unacked_reliable_is_counted` (the first failed on the first run, fixed). |
| **P2** existing tests unchanged on both | **Accepted with documented exceptions** | UDP assertions are unchanged. Changes, each with a reason in the code: (a) a null dereference in a contract test became a FAIL (it crashed); (b) `endpoint_reuse` no longer assumes a closed `PeerId` is reusable on connection-oriented backends (UDP branch keeps the exact old assertion); (c) exact loss counts and exact discard counts are asserted only where the backend declares `exact_loss_accounting`; (d) 18 tests needing raw UDP or asymmetric impairment report **SKIPPED** on GNS (process-global impairment, KI-24), never PASS; (e) the jitter test couples sim time to real time under GNS (`step_wait`, a no-op on UDP). | UDP: unit 15, integration 26, network 12, transport 15. GNS: transport 15, integration 12 P / 14 SKIP, network 8 P / 4 SKIP. ×2. First GNS runs preserved: 7/10, 9 P / 3 F, 6 P / 3 F. |
| **P2** new integration scenarios | **Accepted** | Two clients; reliable ordering and dedup under loss both directions; interleaved reliable/unreliable; backpressure; disconnect with pending reliable; symmetric outage and reconnect; loss/latency/jitter/reorder via the soak profiles. | 5 new contract tests, 1 new integration test, soak gate v4 on both transports (43/43 × 6). |
| **P2** per-transport results | **Accepted** | Gate v4 (transport declared and matched to the host log; thresholds unchanged); separate `tests-*-gns-*` logs; `report.py` lists GNS suites separately. | `docs/soak-testing.md` gate v4 table; two failed GNS soaks preserved. |
| **P3** D-013 | **Evaluated; no dependency added** | `docs/d013-evaluation.md`: recommend DTLS 1.2 (OpenSSL 3.6.5) with RFC 7250 raw public keys pinned by fingerprint over `Unreliable` on both backends; invitation id and version bound via exporter. Fallback: snow. noise-c not recommended (no audit found). | Owner decision pending. Loopback or trusted lab only until then. |

**Problems found while implementing (not raised by the review):** 8 defects, all in EXP-016. The most important: the client ignored a transport `Disconnected` while already reconnecting (it retried a dead connection forever); global GNS impairment outlived its owner; a clean leave became a host timeout because the unreliable `Disconnect` was lost at close.

## Feedback on review #4 (PR #4: transport interface)

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #4 comment 6373680146](https://github.com/M0nsT3rG/ProjectMordor/pull/4#issuecomment-6373680146). ChatGPT reviewed the source only.
- **ChatGPT's verdict:** conditional GO to continue the adapter work. Not approval to merge or to expose the prototype.
- **Implemented on:** `feature/transport-interface`. Experiment record: EXP-015.

| ID | Assumptions verified? | Decision | Implemented in | Verification |
|---|---|---|---|---|
| **FB-4-01** (major) | **Yes.** v1 `send()` was unreliable-only, and the design's `Delivery` classes were missing. | **Accepted** | Contract v2: `Delivery::{Unreliable, ReliableOrdered}`, `TransportCapabilities::reliable_ordered`, and an explicit `SendResult::Unsupported`. A backend never silently downgrades. UDP reports `reliable_ordered=false`. | `contract_reliable_delivery_is_provided_or_explicitly_unsupported`: UDP returns `Unsupported` and sends nothing. GNS must return `Sent` once its adapter exists. |
| **FB-4-02** (major) | **Yes.** Endpoint-only identity cannot express connections, reuse or reconnect. | **Accepted** | `PeerId{id, generation}`: ids are never reused, and the generation increases when an address comes back. Events: `Connected` (always before the first message), `Disconnected`, `Message`, `DroppedInbound`. Host slots and the client's host link store a `PeerId`. A transport `Disconnected` puts the slot into grace (host) or reconnects (client), so the transport can own liveness where it detects it. | `contract_endpoint_reuse_gets_new_peer_generation`, `contract_two_simultaneous_peers_are_routed_separately`, `contract_connected_precedes_first_message_and_bytes_roundtrip`. Disconnect-with-queued-reliable cannot exist on UDP (no reliable class), so it is required for the GNS adapter. |
| **FB-4-03** (minor, security design) | **Yes.** `crypto_kx` is static–static, with no forward secrecy, transcript binding or key confirmation. | **Accepted.** I withdrew my libsodium proposal. | `docs/authentication-design.md` §4a: a precise state machine (Idle, Handshaking, Authenticated, Rejected, Closed); transcript binding; two-way key confirmation; per-direction 64-bit nonce counters that are never reused; a 1024-record replay window; no game message before authentication; re-authentication on every reconnect. D-013 options revised: **TLS 1.3/DTLS via OpenSSL with pinned certificates**, or **Noise IK/XX** via a reviewed implementation; `crypto_kx` plus our own handshake rejected. | Design only. D-013 still needs the user. |
| **FB-4-04** (minor, tests) | **Yes.** Failure semantics were undefined. | **Accepted** | `SendResult::{Sent, Unsupported, NotConnected, TooLarge, Backpressure, Closed, Failed}`; `TransportStats` counts `accepted_then_failed`, `discarded_on_close`, `simulated_drops`, `dropped_inbound_oversized`. Host and client count every non-`Sent` result (`send_errors`), and the host logs the transport counters at stop. | 10 contract tests (new suite `tests/transport`). **Mutation check:** I removed the generation increment and the discard counting, and exactly the two matching tests failed (8/10); restored, 10/10. The moderate soak reports `send_errors=0`, `accepted_then_failed=0`, `discarded_on_close=0`. |
| **FB-4-05** (suggestion) | **Yes.** | **Accepted** | The PR #4 description is reworded: "UDP transport refactor + contract + GNS test hardening". **The GNS host/client is not integrated.** The spike results stay classified separately (EXP-013/014). | n/a |

**UDP compatibility:**
- **Tests:** the UDP test and harness files are unchanged against `42e9451`, and all pass ×3 in both builds.
- **E2E:** passed.
- **Moderate soak, gate v3:** 41/41.

**A mistake of mine caught during implementation:** my rewrite of `Host::update` briefly polled the transport a second time after the timeout checks, which changes the semantics. I caught it before compiling and added `Transport::flush()`, so the original order (receive, then flush) is kept.

---

## Feedback on review request #3 (GameNetworkingSockets spike)

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #3 comment 6373375242](https://github.com/M0nsT3rG/ProjectMordor/pull/3#issuecomment-6373375242). ChatGPT reviewed the source only.
- **ChatGPT's verdict:** conditional GO for implementing the transport interface and further isolated testing.
- **Implemented on:** `feature/transport-interface` (stacked on #3). Experiment record: EXP-014.

| ID | Assumptions verified? | Decision | Implemented in | Verification |
|---|---|---|---|---|
| **FB-3-01** (major, security) | **Yes, and stronger than stated.** In the GNS 1.6.1 open-source build, `IP_AllowWithoutAuth` and `IPLocalHost_AllowWithoutAuth` **default to 2**, so unauthenticated peers are accepted on any IP without warning. Source: `csteamnetworkingsockets.cpp` under `STEAMNETWORKINGSOCKETS_OPENSOURCE`. Trusted-CA loading (`CertStore_AddCertFromBase64`) is internal and not in the public headers. | **Accepted.** The negative baseline is now asserted. The positive-identity, wrong-identity and MITM tests are **planned against the app-layer design**, because GNS native certificates are not usable through the public API. | `tests/gns`: `gns_default_connection_security_flags` asserts encrypted **and** `unauthenticated` on both ends; new `gns_opensource_build_accepts_unauthenticated_peers_by_default` pins the default at 2, so a library update that changes it is caught. The design is in `docs/authentication-design.md`. | Both pass, ×3 and in 3 parallel instances. The prototype stays loopback and trusted-LAN only. |
| **FB-3-02** (major, architecture) | **Yes.** The interface was design-only. | **Accepted, in the recommended order.** The UDP adapter is done; the GNS adapter is next. | `net::Transport`, `UdpTransport`, `make_transport`; Host and Client now hold `std::unique_ptr<Transport>`; `ImpairedSender` takes any transmit function. | **No UDP test or test-harness file changed** (`git diff 42e9451 -- tests/unit tests/integration tests/network tests/test_support.hpp tests/test_main.cpp` is empty); only `tests/gns/` was rewritten, deliberately, for FB-3-01/03/04/05. Unit 15, integration 25 and network 12 pass ×3 in both builds. E2E passes. A lan soak at gate v3 gives 41/41, with RTT 10.5/12.1 ms and recovery 21 ms, the same as before the refactor. |
| **FB-3-03** (minor, tests) | **Yes.** Traffic was sequential, with one pair. | **Accepted at spike level.** Disconnect-mid-backlog and reconnect: **deferred to the adapter**, where they exercise our session layer. | New GNS tests: `gns_concurrent_bidirectional_reliable_ordered` (3000 each way, simultaneously); `gns_two_simultaneous_clients`; `gns_interleaved_reliable_and_unreliable_under_loss`; `gns_send_queue_pressure`. | All pass. Interleaved, 10% loss: reliable 1000/1000 exact; unreliable 900/1000, no duplicates. Queue pressure: 89 661 accepted, **110 339 refused** (explicit backpressure), every accepted message delivered in order. GNS logged "recv queue overflow", so the adapter must drain the receive queue frequently. |
| **FB-3-04** (minor, tests) | **Yes** (TOCTOU). | **Accepted** | `Net` retries the listen up to 8 times with a fresh probed port. | **3 test processes in parallel: 21/21 passed.** The probe stays a test-only mechanism; production code will take an explicit port. |
| **FB-3-05** (minor, isolation) | **Yes.** Callback state and fake config were process-global with no cleanup on throw. | **Accepted** | `GnsLib` RAII resets fake loss, lag and reorder and the global callback even when a CHECK throws. The callback routes through a per-listen-socket registry instead of a global connection handle. | Passes ×3 and in parallel. |
| **FB-3-06** (suggestion, comparability) | **Yes.** | **Accepted: this changes my plan.** I had been about to put `ImpairedSender` *above* GNS. Instead the GNS adapter will use GNS's native fault injection; `ImpairedSender` stays for raw UDP. Both use the same target profiles; realized conditions are logged; gates get transport-labelled versions. No cross-transport latency claims without matched observations. | Recorded in D-012 and `docs/transport-interface.md`. | n/a until the adapter exists |

**Answers adopted:**

- **Answer 1 (authentication).** Pinned per-player public keys, exchanged out of band and bound to a session invitation, with explicit fingerprint confirmation. Verified against the real library: GNS cannot provide this through its public API, so the design puts end-to-end authenticated encryption at our protocol layer, using a vetted library (libsodium proposed). **Library choice D-013 is pending with the user.**
- **Answer 2 (session layer).** On GNS, GNS owns transport liveness and reconnect. Game identity, slots and grace stay in the application. A reconnect means a new transport connection plus re-authentication.
- **Answer 3 (comparability).** See FB-3-06.
- **Answer 4 (integration order).** Followed: interface plus UDP first, then the GNS adapter. GNS stays default OFF.

---

## Follow-up verification of review #2

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #2 comment 6372799837](https://github.com/M0nsT3rG/ProjectMordor/pull/2#issuecomment-6372799837). ChatGPT verified the source only.
- **ChatGPT's verdict:** no new blocking correctness defect; conditional acceptance for continuing Phase 2A lab work.

| Item | Assumptions verified? | Decision | Implemented in | Verification |
|---|---|---|---|---|
| **FU-2-1: manifest uniqueness, names and cardinality; match the chosen scenario** | **Yes.** v2 checked only for a non-empty list. A duplicate (`client1, client1, client2`) produced duplicated per-client gates, and a self-consistent but different manifest was trusted. | **Accepted** as gate **v3** | `run.json` names a `scenario`, and `analyze.py` has a fixed `SCENARIOS` table. New gates: `manifest_names_valid` (`^client[1-9][0-9]{0,2}$`), `manifest_names_unique`, `scenario_known`, `manifest_matches_scenario`, `stall_plan_matches_scenario`. Each client is gated once even if listed twice. | 6 new self-checks: duplicates; invalid names (`../host`, empty, the integer `7`, `client0`); unknown or missing scenario; a self-consistent one-client manifest that differs from the scenario; stall-plan mismatch; window rates. **Writing them exposed a defect in the new code:** a non-string name crashed the analyzer (`TypeError` in `sorted`) instead of failing a gate. Fixed. Self-checks: **17/17**. |
| **FU-2-2: bandwidth gate is a run-wide mean, not burst/peak** | **Yes.** | **Accepted**: relabelled. Peak gate **deferred** with a measured reason. | Gates renamed `*_download_mean_budget` and `*_upload_mean_budget`, and their detail text says "mean-rate gate, not a peak gate". Client status lines now carry cumulative bytes. `window_rates_kbit_s` reports the p95, p99 and peak of the 1 s window rates. These are reported, not gated. | **The v3 soak shows why a peak gate needs its own bound.** In moderate and harsh, 1 s download peaks **exceed the mean ceiling**: 23.95 against 23.39 kbit/s, and 24.84 against 24.08. Jitter bunching and duplication cause short bursts above the mean. A peak gate will be a later gate version with a separately derived bound (KI-20). |

**v3 soak** (real-time, 120 s, fresh `-g3` directories, 41 gates each):

| Run | Gates | RTT p50 / p99 | Mean in / out | 1 s window peak in / out | Recovery | Shed |
|---|---|---|---|---|---|---|
| `soak-2026-10-23-212948-lan-g3` | 41/41 | 10.5 / 12.1 ms | 21.6 / 15.1 | 21.9 / 15.4 | 21 ms | 0 |
| `soak-2026-10-23-213209-moderate-g3` | 41/41 | 104.3 / 120.5 ms | 21.1 / 22.3 | 23.9 / 23.7 | 154 ms | 9 |
| `soak-2026-10-23-213417-harsh-g3` | 41/41 | 204.6 / 237.5 ms | 19.3 / 29.4 | 24.8 / 29.6 | 1328 ms | 107 |

The host's `timeout_notices` counter is now logged: 1 per run, as expected for one stall. Experiment record: EXP-012. The v1 and v2 evidence is untouched.

---

## Feedback on review request #2 (real-time soak)

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #2 comment 6372084762](https://github.com/M0nsT3rG/ProjectMordor/pull/2#issuecomment-6372084762), posted from the repository owner's account. ChatGPT reviewed the source code only and did not run anything.
- **ChatGPT's verdict:** conditional GO for more Phase 2A lab work. It recommends hardening the analyzer and the timeout notice before calling the soak gates robust.

### Triage by Claude Code

Each finding was verified against the code first. Experiment record: EXP-011.

| Feedback ID | Assumptions verified? | Decision | Reason | Implemented in | Verification |
|---|---|---|---|---|---|
| **FB-2-01** (major, P0) | **Yes, demonstrated.** Analyzer v1 iterated `glob("client*.log")`, gated only clients it found, and compared joins with that found count; client2's stall gates were skipped when its log was absent. Run on a synthetic run where client2 never started (no log, `exit_codes.client2 = null`, host joins = 1), **v1 printed "SOAK PASSED"**. | **Accepted** | Gates must not depend on which files happen to exist. | Gate **version 2**: `run.json` carries `expected_clients`; new gates `manifest_lists_expected_clients`, `client_logs_match_manifest` (missing, renamed and extra logs all fail), `stalled_clients_in_manifest`, `exit_codes_cover_all_processes`, `host_joins_match_manifest`; every expected client is gated whether or not its log exists. Also, **evidence immutability**: the analyzer refuses to overwrite an existing `summary.json`, and `soak.ps1` writes every run to a fresh timestamped `…-g2` directory and never deletes. (The old script emptied the run directory and would have overwritten the failing evidence on the next run of the same profile.) | 8 new analyzer self-checks (missing, renamed and extra logs; missing manifest; joins against the manifest; no overwrite; budgets and coverage) plus the existing ones: **11/11**. v2 fails the synthetic broken run that v1 passed. |
| **FB-2-02** (major, P1) | **Yes, a real defect found.** The client checked endpoint, session and state, but not ordering. A `Disconnect{Timeout}` sent while the slot was in grace but delivered *after* a successful reconnect knocked the fresh connection back into Reconnecting. | **Accepted** for robustness. Authenticity: **deferred**; agreed it needs an authenticated transport. | The client records the sequence of the `ConnectAccept` that established the current connection and ignores any `Disconnect` with that sequence or older (`stale_notices`). This is a correctness guard, not a security control: a forged packet can still claim a newer sequence, so the prototype stays loopback and trusted-LAN only (KI-04). No nonce-only "security fix" was invented, as ChatGPT also advised. | `Client::handle` (`accept_seq_`), `ClientStats::stale_notices` | Fake-host tests written first: `delayed_timeout_notice_after_reconnect_is_ignored` and `replayed_and_duplicate_timeout_notices_have_no_extra_effect` **failed on the unfixed client** and pass now. `timeout_notice_from_other_session_or_endpoint_is_ignored` passes (session and endpoint were already checked). The real-host `timeout_notice_recovery_under_jitter_reconnects_once` passed before and after: it delivered no late notice (`stale_notices=0`), so it is labelled a smoke test, not proof. |
| **FB-2-03** (major) | **Yes.** `max_correction`, shed inputs and kbit/s were only reported. | **Accepted** for bandwidth, correction and coverage. Shed-input threshold: **deferred**. | The ceilings are **derived from the protocol design before any v2 run**, not fitted to results. **Download:** (87-byte snapshot × 30 Hz + 30-byte ack × 4 Hz) × (1 + duplicate) × 1.05. **Upload:** (119-byte full input batch × 30 Hz + 30-byte heartbeat × 4 Hz) × 1.05. **Correction:** speed × (RTT bound + 3-tick backlog), because reconciliation only replays unacknowledged inputs. **Coverage:** elapsed ≥ 95% of the duration, and RTT samples ≥ 50% of 4/s × duration. Correction percentiles (p50, p99) are now logged. A shed-input or queue-depth threshold has no design-derived bound yet and needs repeated baselines first. | `tools/soak/analyze.py` (v2), client summary (`correction_p50/p99`, `stale_notices`) | Self-check `test_budgets_follow_protocol_arithmetic` pins the arithmetic, and each new gate has a test proving it can fail. The v2 soak passed all 36 gates on all 3 profiles (results below). |
| **FB-2-04** (minor) | **Yes.** | **Accepted** (labelling). Tighter benchmark gate: **deferred.** | The 60 ms allowance is kept at the v1 value for comparability and is now labelled in code and gate output as a "synthetic loopback bound", not a LAN criterion. Gates are versioned, and old summaries keep `gate_version` absent (v1). A tighter lab benchmark needs repeated measurements and will be a new gate version. LAN gets its own baseline once it can be run. | `analyze.py` comments, gate detail text, `gate_version` | n/a |
| **FB-2-05** (minor) | **Yes.** There was one 5 s in-process stall in a 120 s run. | **Accepted, deferred** (P1 for the next soak iteration) | Agreed coverage gaps: host stalls, repeated and asymmetric outages, OS-level suspension, startup races, repeated reconnects within the grace period, longer randomized seeded runs. Out of scope for this fix round. | KNOWN_ISSUES KI-16 | — |
| **FB-2-06** (suggestion) | **Yes.** The analyzer comment still described the 15.6 ms sleep situation. | **Accepted** | The comment was updated to give the v1 rationale and the post-fix reality (about 4 ms). The analyzer now records each client's `high_resolution_timer` flag from its `loop_timer` log line. Before/after traces are preserved (EXP-010 directories untouched). | `analyze.py` | n/a |

### Answer 4: reliable event channel

- **ChatGPT's recommendation:** prefer **ENet or GameNetworkingSockets behind a small transport interface** over a custom reliability protocol.
- **Decision:** accepted as the direction, **pending a user decision**. Vendoring either library adds third-party code and licence review (ENet is MIT; GameNetworkingSockets is BSD-3). Project rules say the user decides on new dependencies.
- **What already exists:** before review #2 arrived I had started a minimal in-house `ReliableChannel` (protocol v2 `Event`/`EventAck`, 21 passing unit tests, including a 5-seed × 2000-event lossy/reordering property test). It is parked as an **unpushed local WIP commit** on `feature/reliable-events` and is not part of PR #2.
- **Proposal:** use it as (a) the transport-neutral interface and test oracle, and (b) one of the candidates measured against ENet and GameNetworkingSockets with the same event-order, deduplication, reconnect and fault-injection tests, as ChatGPT suggests.

### Soak results under gate v2

All runs are real-time, 120 s, 36 gates each.

| Run | Gates | RTT p50 / p99 | Max correction (budget) | In / out kbit/s (budgets) | Recovery after stall | Shed |
|---|---|---|---|---|---|---|
| `soak-2026-10-23-204617-lan-g2` | **passed** | 10.5 / 12.1 ms | 0 (0.84) | 21.6 / 13.7 (22.9 / 31.0) | 18 ms | 0 |
| `soak-2026-10-23-204825-moderate-g2` | **passed** | 103.9 / 121.6 ms | 0.33 (1.40) | 21.1 / 22.2 (23.4 / 31.0) | 161 ms | 8 |
| `soak-2026-10-23-205033-harsh-g2` | **passed** | 203.9 / 241.6 ms | 0.33 (2.00) | 19.2 / 29.4 (24.1 / 31.0) | 1567 ms | 110 |

**Caveats:**
- Harsh recovery varied between runs: 572 ms in EXP-010 and 1567 ms here, both under the 3000 ms gate. That variance is expected at 15% loss in both directions and is not yet characterised statistically.
- Harsh upload is close to its ceiling (29.4 of 31.0). That is expected: at RTT of 200 ms or more every input batch carries the full 8 redundant commands, so it sits at the design maximum.
- The host's `timeout_notices` counter was missing from the `host_stopped` log during these runs. It has been added since, so it appears only in future runs.

---

---

## Feedback on review request #1

- **Status:** received and triaged (2026-10-23).
- **Source:** [PR #1 comment 6371152419](https://github.com/M0nsT3rG/ProjectMordor/pull/1#issuecomment-6371152419), posted from the repository owner's account.
- **ChatGPT's verdict:** conditional GO for Phase 2A (standalone networking hardening); NO-GO for Steam game integration under the current constraints.
- **ChatGPT's stated limit:** it reviewed documents and source code only. It did not build or run anything.

### Summary of findings received

| ID | Area | Severity | Gist |
|---|---|---|---|
| FB-1-01 | protocol | major | `Host::step` drains the backlog by calling `apply_input(dt)` several times in one tick, so entity time runs ahead of world time |
| FB-1-02 | protocol | major | Unauthenticated token and endpoint identity; liveness was refreshed before checking the message's direction |
| FB-1-03 | tests | major | 38/38 passes are simulated-clock; real-time soak, process-pause and LAN tests are missing; a backlog test is the top priority |
| FB-1-04 | architecture | suggestion | Keep unreliable movement; add a separate reliable, ordered, idempotent event channel |
| FB-1-05 | evidence | minor | Make "CONFIRMED" explicitly scoped (`verification_scope`) |
| FB-1-06 | feasibility | minor | Keep G0 NO-GO explicitly conditional; seek jurisdiction-specific advice; GOG unverified |
| FB-1-07 | protocol | minor | The 30 s grace period exceeds the 15 s retry window; add adversarial reconnect tests |

### Triage by Claude Code

Every recommendation was checked against the code or documents before any action. Commit and test details are in [DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md) and EXP-009.

| Feedback ID | Assumptions verified? | Decision | Reason | Implemented in | Verification |
|---|---|---|---|---|---|
| **FB-1-01** (P0) | **Yes, with one correction.** `src/host/host.cpp` `Host::step` ran a `do { apply_input(dt) } while (pending > 2)` loop inside one tick: confirmed. The secondary claim that client replay "can diverge" is **not correct for the old code**. The client applies each input once and the host also applied each input exactly once, so final positions matched and reconciliation stayed consistent. The real defect is the **per-tick time budget**: within one tick an entity could move up to N × one timestep. That would break future collision, combat timing and AI, and it shows remote players a speed burst. | **Accepted** | A fixed tick must advance every entity by exactly one timestep. Policy (D-010): one input per tick; any backlog above 2 is shed oldest-first and acknowledged as consumed, so the client stops replaying it and reconciliation absorbs the difference. Rejected alternative: a "global catch-up budget" that advances world time, because it complicates authority for no benefit at this stage. Deferred: client-side input-rate adaptation. | `Host::step`, `HostStats::shed_inputs`, `docs/protocol.md`, D-010 | Tests written first and **failed on the old code**, then pass: `input_burst_never_exceeds_one_timestep_per_tick` (exactly 3 of 16 burst inputs simulated, 13 shed, one tick per step), `steady_inputs_are_never_shed`, `jitter_never_exceeds_tick_movement_budget` (80 ms jitter; worst per-tick displacement ≤ 1.0 timestep; prediction converges after stop) |
| **FB-1-02** | **Yes.** `Host::handle` set `last_recv_us` before the message-type check, and so did `Client::handle`. The token is a non-cryptographic 64-bit value that allows slot migration: confirmed, and already documented as "not authentication" (R-12). | **Partially accepted, partially deferred.** State-aware liveness: accepted and done. Authentication, integrity and rate limiting: deferred. | Liveness is a correctness bug, cheap to fix. Authenticated transport is a large design change that only matters before WAN or untrusted-LAN exposure, which project rules already forbid (D-006). It is tracked as a hard prerequisite for internet use. | `Host::handle`, `Client::handle` (direction filters); KNOWN_ISSUES KI-04 | `wrong_direction_messages_do_not_refresh_liveness` and `client_ignores_wrong_direction_messages_for_liveness` failed before the fix and pass after. `reconnect_token_migrates_slot_to_new_endpoint` pins down the documented (insecure) token behaviour. |
| **FB-1-03** | **Yes.** All suites use the simulated clock; real-time coverage is one E2E run; the LAN test is NOT_RUN. | **Partially accepted.** Backlog test: done. Labelling simulated versus real time: done. Real-time soak, process-pause and telemetry: deferred (P1). LAN: still blocked on firewall approval. | The P0 part is done. A soak with p95/p99 RTT and drift telemetry is a separate work item. | `tools/collab/report.py` (a Clock column plus a real-time note in TEST_RESULTS) | The report shows "simulated clock" per suite. Real-time E2E is kept separate. |
| **FB-1-04** | **Yes.** All 8 message types are unreliable (`docs/protocol.md`). | **Accepted as design direction, deferred** (Phase 2A P2) | Agreed: movement stays unreliable; consequential events need a reliable, ordered, idempotent plane. Nothing in the current scope sends such events yet, so building it now would be speculative. | D-010 (decision recorded); KNOWN_ISSUES KI-13 | n/a (design only) |
| **FB-1-05** | **Yes.** 33 CONFIRMED findings mixed our own measurements with statements about what sources contain. | **Accepted** | A cheap, high-value integrity improvement. Rule: `locally_measured` (F-ENV), `source_verified` (other CONFIRMED rows: we checked what the source says), `third_party_claim` (everything else). The validator now **rejects** CONFIRMED + `third_party_claim`. | All 62 findings (`verification_scope`), `tools/evidence/evidence.py`, `report.py` tables, CLAUDE.md conventions | Migration diff is whitespace plus 62 added lines; a semantic JSON comparison against HEAD showed 0 content changes. The validator reports 71 records, 0 errors, and a negative test confirms both new rules fire. Distribution: 29 `source_verified`, 25 `third_party_claim`, 8 `locally_measured`. |
| **FB-1-06** | **Yes.** The phase 1 report already says "not a technical verdict … unknown, not disproven", the matrix makes G0 conditional on D-007/D-008, and GOG is marked unverified. | **Accepted, no change needed** | The documents already reflect it. ChatGPT also confirms circumvention stays off the table. Its note "avoid inferring absence of a mechanism from absent static imports" is already scoped in F-ENV-006, which says absence from static imports proves nothing about runtime use. | — | Re-read `docs/phase1-report.md` §12 and `docs/feasibility-matrix.md` G0 |
| **FB-1-07** | **Yes.** 30 s grace versus a 15 s client window; `Host::on_connect` endpoint and token handling. | **Accepted** (documentation and tests). User-initiated retry: **deferred.** | The trade-off is intentional (capacity reservation) and now documented. The edge-case tests are added. Several suggested cases were already covered: old-nonce responses (`client_rejects_stale_snapshots` sends a wrong-nonce accept), sustained loss, and timeout/reconnect. | `docs/protocol.md` (capacity trade-off); KNOWN_ISSUES KI-11; tests | New tests: `reconnect_token_migrates_slot_to_new_endpoint`, `reconnect_after_grace_expiry_gets_new_identity`, `client_survives_host_restart_with_new_session`. One of them first failed because of a **test** bug: with only the uplink cut, the client correctly stays Connected until the host stops sending. That behaviour is recorded as KI-12. |

### Rejected recommendations

None outright. Two parts were narrowed:

- **FB-1-01:** the "global catch-up budget that advances world time" option was rejected in favour of shedding. See the reason above.
- **FB-1-01 secondary claim:** the claim that client replay can diverge was corrected; replay was consistent, and the actual defect was the per-tick time budget.

### Problems found while implementing (not raised by the review)

- **Exclusive log files.** `fopen_s` opens files exclusively. A concurrent reader of `host.log` made the host fail with "cannot open log file", and nothing could read a live log. Fixed: logs are now opened with `_fsopen(..., _SH_DENYWR)` via `cli::open_log`.
- **Stale wait in `test.ps1`.** The E2E wait loop polled the **previous run's** `host.log`, so the EXP-006 mitigation never really worked. Fixed: old logs are deleted first.
- **First run after a relink.** EXP-006 was re-tested and reproduced: the first run after a relink exchanged no packets. That **contradicts** the earlier "late start" hypothesis. The root cause is unknown. A warm-up run avoids it, and it is now part of `test.ps1` (KNOWN_ISSUES KI-08).

### Answers to ChatGPT's suggested priorities

| Priority | Item | Status |
|---|---|---|
| P0 | Backlog timestep correctness plus regression test | **Done** |
| P1 | Peer and message-state validation; reconnect tests | **Done** for liveness and edge cases. Authentication and rate limiting deferred. |
| P1 | Real-time soak and telemetry tests | Open |
| P2 | Reliable ordered idempotent events | Decision recorded (D-010); implementation open |
| P2 | Timer and RTT overhead, desync digests, LAN test (with approval) | Open |
