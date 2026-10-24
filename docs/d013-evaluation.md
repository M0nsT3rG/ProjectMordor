# D-013 evaluation: authenticated peers (review #4 follow-up, priority 3)

**Status:** evaluation and recommendation only. **No crypto dependency has been added and no handshake has been implemented.** The decision belongs to the repository owner. Until authentication is implemented and independently validated, the prototype stays on **loopback or explicitly trusted lab networks**.

Required properties, from `docs/authentication-design.md` §4a:
- mutual authentication with pinned identities;
- forward secrecy;
- transcript or session binding (invitation id, protocol version);
- key confirmation;
- replay protection and nonce-reuse safety;
- re-authentication on every reconnect;
- no custom cryptography.

## Facts verified for this evaluation (2026-10-23)

| Fact | Source | Scope |
|---|---|---|
| OpenSSL 3.6.5 (already built for the GNS build) offers DTLS **1.0 and 1.2 only**: `DTLS_method(void); /* DTLS 1.0 and 1.2 */`. No DTLS 1.3. | `include/openssl/ssl.h` in `build-gns/vcpkg_installed` | source_verified |
| It supports **raw public keys** (RFC 7250) through `SSL_set1_client_cert_type` and `SSL_CTX_set1_server_cert_type`, so peers can pin a public key without X.509 certificates. | same header | source_verified (the API exists; not exercised yet) |
| TLS 1.3 is available but needs a reliable, ordered byte stream. Our UDP backend has no reliable class (KI-23); GNS has one. | same header; Transport capabilities | source_verified |
| **snow** (Rust Noise implementation) had an independent assessment by Trail of Bits, commissioned by 1Password; 8 of 10 findings resolved, 2 informational items open. | [1Password blog](https://1password.com/blog/strengthening-snow-for-the-open-source-community) | third_party_claim |
| I found **no** public audit for **noise-c**. | web search 2026-10-23 (absence of evidence, not evidence of absence) | unverified |
| **Noise\*** compiles Noise patterns to verified C with correctness and security proofs (MSR/INRIA, IEEE S&P 2022). | [IACR ePrint 2022/607](https://eprint.iacr.org/2022/607) | third_party_claim |

## Options

| Option | Forward secrecy | Transcript / session binding | Key confirmation | Replay protection | Reconnect auth | Fit with our code | Risks / costs |
|---|---|---|---|---|---|---|---|
| **A. DTLS 1.2 (OpenSSL 3.6.5) with RFC 7250 raw public keys, pinned by fingerprint, over the Transport's `Unreliable` class** | yes (ECDHE suites only) | handshake transcript via Finished. Invitation id and protocol version bound by an exporter value (`SSL_export_keying_material`) confirmed as the first message inside the channel. | yes (Finished) | built-in DTLS anti-replay window | a new DTLS handshake per transport connection | **Same on both backends**: it only needs datagrams, and both backends provide `Unreliable`. C API, already compiled in the GNS build. | DTLS 1.2, not 1.3. Large dependency, and the default build would need vcpkg/OpenSSL. Cipher suites must be restricted to ECDHE+AEAD. RPK with DTLS must be proven by tests before relying on it. Double encryption on GNS (acceptable, defence in depth). |
| **B. Noise IK/XX via snow (Rust), through an FFI wrapper** | yes | Noise transcript hash (prologue = invitation id and version) | yes (pattern-dependent) | Noise transport nonces; we add a replay window for unreliable delivery | a new handshake per connection | Needs a Rust toolchain and an FFI layer in a C++ project | New language and toolchain. The FFI is our code to review. Noise transport messages assume in-order delivery unless explicit nonces are used (needs care on unreliable delivery). |
| **C. Noise via noise-c** | yes | yes | yes | as B | as B | C, easy to link | **No audit found.** Not recommended until its review status is established. |
| **D. Noise\* generated verified C** | yes | yes | yes | as B | as B | Generated C | Research tooling; integration effort and maintenance unknown. Worth tracking, not first choice. |
| ~~E. libsodium `crypto_kx` plus our own handshake~~ | no | no | no | ours | ours | — | **Rejected** (review #4 FB-4-03): custom protocol design. |

## Recommendation (for the owner's approval)

**Option A: DTLS 1.2 via OpenSSL with RFC 7250 raw public keys, pinned by confirmed fingerprints, carried over the Transport's `Unreliable` delivery on both backends.**

**Why:**
- It is the only option that is standardised, has heavily reviewed C implementations, and works the same on raw UDP and GNS.
- It needs no new language toolchain.
- It already exists in the dependency set built for GNS.
- It gives forward secrecy (ECDHE), key confirmation and built-in anti-replay.
- The project-specific binding (invitation id, protocol version) uses a standard exporter instead of custom crypto.

**Conditions before implementation:**
1. The owner approves OpenSSL as a dependency of the authenticated build. It cannot stay dependency-free.
2. Prove by tests: RPK negotiation with DTLS 1.2 in OpenSSL 3.6.5; the cipher-suite restriction (ECDHE+AEAD only); the anti-replay window; the handshake-failure paths (wrong key, MITM relay, downgrade attempt).
3. Independent review of the integration before any untrusted-network use.

**Fallback.** If RPK + DTLS 1.2 turns out impractical in tests, use **Option B (snow)**, accepting the Rust toolchain.

## Proof test (review #5 FB-5-05, 2026-10-24)

The owner approved a minimal proof test with the vcpkg OpenSSL, now a **direct** dependency of the optional GNS build (`vcpkg.json`). It lives in `tests/dtls/dtls_proof_tests.cpp` (suite `dtls`). Both peers run in one process over datagram-preserving memory BIOs (`BIO_s_dgram_mem`): no sockets, no network. EXP-017 holds the record.

| Case | Result |
|---|---|
| DTLS 1.2, RPK in both directions, pins correct | handshake completes. `DTLSv1.2`, `ECDHE-ECDSA-AES128-GCM-SHA256`. Negotiated cert type is RPK on both sides; no X.509 certificate; the peer key's fingerprint equals the pin; data flows. |
| Host presents a different key (relay/MITM) | rejected by the client (verify result not OK) |
| Client presents a different key | rejected by the host |
| Peer offers no raw public key (downgrade to X.509 / none) | rejected (handshake failure) |
| Peer offers DTLS 1.0 only (version downgrade) | rejected (protocol version alert) |
| Peer offers only an ECDHE CBC suite (non-AEAD) | rejected (handshake failure) |
| Exporter (`EXPORTER-mordor-invitation`, context = invitation id) | equal on both sides; different for another invitation; different for every new handshake with the same keys (fresh ECDHE) |
| Replayed record; tampered record | both dropped; the channel keeps working |

**First run failed** (all 8 tests at `SSL_add_expected_rpk`; log `tests-dtls-first-run-2026-10-24.json`). OpenSSL stores the pin as a DANE-EE SPKI record, so DANE must be enabled first: `SSL_CTX_dane_enable` + `SSL_dane_enable(ssl, NULL)`. The header alone did not show this: exactly the gap FB-5-05 pointed at.

**What this does NOT prove:**
- integration with the Transport (handshake over real `Unreliable` delivery, with loss and retransmission timers);
- the binding message exchange inside the channel;
- invitation expiry (an application rule, not crypto);
- cookie exchange (`DTLSv1_listen`) against address spoofing;
- behaviour under packet loss or reordering during the handshake;
- an independent review.

OpenSSL's error queue is per thread and both peers share one thread here, so the printed reasons can mix both sides. The tests assert `SSL_get_verify_result` on the side that must reject.

## DTLS over a real transport, with the invitation binding (review #6 FB-6-01/02, 2026-10-24)

`src/network/dtls_session.{hpp,cpp}` (GNS build only) runs one DTLS 1.2 session per peer. Records travel as plain `Transport::Unreliable` payloads on **both** backends. OpenSSL's own timers drive retransmission (`DTLSv1_get_timeout`/`DTLSv1_handle_timeout`). GNS reliable delivery is deliberately **not** used to carry DTLS: DTLS is designed for datagrams, and one code path for both backends keeps the security layer independent of the transport (review #6 answer 4).

**Fail-closed binding (FB-6-01).** After the handshake, each side sends inside the channel the invitation it holds (id, protocol version, expiry) and a confirmation value: the TLS exporter (RFC 5705, label `EXPORTER-mordor-invitation-binding`, context = that invitation).
- The receiver accepts only if every field equals its own invitation, its own invitation has not expired, and the confirmation equals its own exporter value (`CRYPTO_memcmp`).
- Application data before that is counted and dropped.
- A binding is resent every 200 ms until the peer's arrives; with no binding after 5 s the session is rejected.
- No custom crypto: identity is the pinned key, and binding and freshness come from DTLS and its exporter.

**Other rules:**
- **Cookies:** HelloVerifyRequest cookies on every server session, as HMAC-SHA256 over the peer id and address with a per-process random key (answer 5).
- **Renegotiation:** disabled.
- **Reconnect from the same address (UDP):** an epoch-0 ClientHello on an established association opens a **pending** session next to it. The pending session is promoted only once it authenticates, so a spoofed ClientHello cannot tear down a valid session.

Suite `dtls`: 17 tests, the 8 in-memory proofs above plus 9 over a real link, each run on UDP and GNS.

| Case (both backends) | Result |
|---|---|
| Pinned keys + binding | authenticated; fingerprints = pins; same confirmation on both sides; ≥1 cookie verified; data both ways |
| 25% loss, 20% duplication, 20 ms latency, 30 ms jitter (reorders) | authenticated; retransmission timeouts handled (UDP 3, seeded; GNS 2–5); app records never delivered twice |
| Wrong pinned key | rejected (`no matching DANE TLSA records`) |
| Invitation id / version / expiry mismatch | **both** sides rejected with the specific reason; no application data |
| Peer never sends its confirmation | host drops its data (`app_dropped_before_auth`), then `binding_timeout` |
| Expired invitation (client clock ahead) | client refuses before handshaking; the host never authenticates |
| Reconnect | new handshake and a fresh confirmation (UDP: pending session promoted; GNS: new connection) |
| Spurious ClientHello on an established UDP association | the pending session fails; the established one is untouched and keeps working (n/a on GNS: nobody can inject into a connection) |
| MTU 256 | authenticated; no datagram above 256 bytes |

**First run: 15/16** (log `tests-dtls-transport-first-run-2026-10-24.json`). A UDP reconnect from the same address hit the established session, which ignored the new ClientHello. Fixed by the pending-session rule above.

## Secure transport decorator, game traffic over DTLS (review #7, 2026-10-24)

`src/network/secure_transport.{hpp,cpp}` (D-015) implements `Transport` on top of any backend. Host/Client opt in with `HostConfig::secure` / `ClientConfig::secure`. **There is no plaintext fallback:** if the build or the config cannot provide security, `start()` fails.

**Gating.** `Connected` is reported, and messages are delivered, only after DTLS and the binding succeed. A non-DTLS datagram from a peer without a session is counted (`dropped_not_dtls`) and dropped. `ReliableOrdered` returns `Unsupported`.

**Pre-authentication state is bounded (FB-7-01):**
- one pending session per inner peer;
- `max_pending_total` overall (default 4);
- each pending session lives at most `pending_lifetime_us` (default 10 s).

The HelloVerifyRequest cookie is **stateful**: a session exists before it. It is a handshake-integrity measure, **not** a stateless DoS gate.

**Reconnect from the same address (FB-7-05).**
- It runs as a pending session and is promoted only once authenticated.
- The old association is then retired: the host sees `Disconnected("replaced_by_new_session")`, then `Connected` under a **new** PeerId.
- Late records of the retired session are dropped.
- A peer with a different invitation never replaces anything.

**Retransmission policy (FB-7-02).**
- OpenSSL's default (1 s doubling to 60 s) left about 4 attempts in a 15 s budget. Under 25% loss that made `dtls_over_transport_survives…` fail once and `dtls_timer_fires_with_late_irregular_service` fail on UDP (preserved: `tests-dtls-review7-first-run-2026-10-24.json`).
- The back-off is now set through OpenSSL's own `DTLS_set_timer_cb`: 250 ms doubling to a 2 s cap.
- `service()` fires an expired timer even when it is called late.

| Test (both backends unless noted) | Result |
|---|---|
| Game session over DTLS (2 players) | both connected, snapshots and remote entities flow, `dropped_not_dtls` = 0 |
| Plaintext client vs secure host | refused (13 non-DTLS datagrams dropped); client ends `connect_deadline` |
| Secure client vs plaintext host | never connects (`connect_deadline`) |
| Unpinned player, wrong invitation | `transport_handshake_failed`; no slot on the host |
| Outage and reconnect | same player id via the game token, after a new DTLS handshake (UDP: replaced association) |
| Pending cap and expiry | 6 unpinned handshakes against a cap of 3 are refused beyond it; all expire; a legitimate player then connects |
| Same-address reconnects ×2 (UDP) | each replaces the association only once authenticated; late old records dropped; wrong invitation ignored |
| Late, irregular `service()` (every 250 ms) | authenticated; timer fired on late calls |
| 3 s total loss during the handshake | recovers; 3 retransmits during the outage, about 2,000 service calls (no busy loop) |
| Permanent loss, 4 s budget | `handshake_timeout`; 4 retransmits |
| Altered confirmation | `binding_confirmation_mismatch` |
| 50% duplication, 20% loss, 60 ms jitter | authenticated; bindings sent ≤ 5 per side; replies rate-limited |
| Invitation expiring during a long handshake | refused (`invitation_expired`) |

**Status:** the evidence supports Option A as the working choice for the next step (integration behind the Transport). > **Correction (EXP-021, KI-34).** Until EXP-021 every DTLS datagram was sent with `now=0`, so on **UDP** the simulated latency and jitter were not applied to DTLS traffic; loss and duplication were. The UDP rows above that mention latency or jitter (EXP-018/019) therefore really covered loss and duplication only. GNS rows are unaffected. After the fix, all DTLS tests were rerun with real latency and jitter on UDP and pass (EXP-021).

## Review #8 follow-up (2026-10-24, EXP-021)

- **Executables in secure mode (FB-8-03, D-016).**
  - `mordor-keygen` creates DPAPI-protected, user-only key files and public-only invitations.
  - `mordor-host` / `mordor-client` take `--secure-key`, `--invitation` and `--allow-peer`, and fail closed.
  - `scripts/secure-e2e.ps1` runs host plus 2 clients over DTLS, then 5 negative cases (plaintext, stranger key, wrong invitation, expired invitation, missing key) on UDP and GNS.
  - `soak.ps1 -Secure` runs the soak over DTLS.
- **Secure reliable channel (FB-8-02).**
  - `ReliableOrdered` inside DTLS: exact and in order under 15% loss plus duplication in both directions;
  - `Backpressure` at 256 messages; accepted-then-lost messages counted exactly;
  - bounded close drain;
  - only DTLS records on the wire (content types 20–23 checked).
- **Pending-session robustness (FB-8-01).**
  - A forged ClientHello can displace only a pending session without a verified cookie.
  - Established players reconnect in a reserved slot outside the cap.
  - Under continuous forgeries from the player's own address, the reconnect took about 50 ms (bounded by the 10 s lifetime before).
- **Idle timeout with keepalives (FB-8-04).**

D-013 is **not** closed. Still required:
- **Cookies:** a stateless cookie before allocating any per-peer state (KI-30);
- **Review:** an independent review;
- **Tests:** a real LAN test.

Until then the prototype stays on loopback or trusted lab networks.

## Sources

- 1Password, "Strengthening snow for the open-source community": https://1password.com/blog/strengthening-snow-for-the-open-source-community
- Ho, Protzenko, Bichhawat, Bhargavan, "Noise\*: A Library of Verified High-Performance Secure Channel Protocol Implementations", IACR ePrint 2022/607: https://eprint.iacr.org/2022/607
- OpenSSL 3.6.5 headers (`ssl.h`), as built by vcpkg in this repository's GNS build.
