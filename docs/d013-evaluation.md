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

**Status:** the evidence supports Option A as the working choice for the next step (integration behind the Transport). D-013 is **not** closed: the integration and an independent review are still required, and until then the prototype stays on loopback or trusted lab networks.

## Sources

- 1Password, "Strengthening snow for the open-source community": https://1password.com/blog/strengthening-snow-for-the-open-source-community
- Ho, Protzenko, Bichhawat, Bhargavan, "Noise\*: A Library of Verified High-Performance Secure Channel Protocol Implementations", IACR ePrint 2022/607: https://eprint.iacr.org/2022/607
- OpenSSL 3.6.5 headers (`ssl.h`), as built by vcpkg in this repository's GNS build.
