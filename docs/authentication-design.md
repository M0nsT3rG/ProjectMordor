# Authenticated peers: design (review #3, FB-3-01 and answer 1)

**Status:** design only. Nothing here is implemented. Choosing a crypto library is a new dependency and needs the user's approval (D-013, pending). Until this design is implemented and tested, **the prototype must not be exposed to any untrusted network** (KI-04, KI-17).

## 1. Verified facts about GameNetworkingSockets 1.6.1

These were checked in the installed headers and in the vcpkg source tree on 2026-10-23.

| Fact | Where | Scope |
|---|---|---|
| Plain-IP connections get encryption, but "without certificates (or a shared secret …) you don't have any way of knowing who is actually on the other end, and thus are vulnerable to man-in-the-middle attacks". | `steam/isteamnetworkingsockets.h`, comment on `ConnectByIPAddress` | source_verified |
| **In the open-source build, unauthenticated peers are accepted by default on any IP address, without warnings:** `IP_AllowWithoutAuth = 2` and `IPLocalHost_AllowWithoutAuth = 2` under `STEAMNETWORKINGSOCKETS_OPENSOURCE`. The source comments that there is no trusted third party. In the non-open-source build the default is 0. | `src/steamnetworkingsockets/clientlib/csteamnetworkingsockets.cpp` lines 87–95 (v1.6.1) | source_verified; the runtime value is measured by the test `gns_opensource_build_accepts_unauthenticated_peers_by_default` |
| Certificate authentication exists internally: `CertStore_CheckCert`, and `CertStore_AddCertFromBase64` adds trusted CAs. **These are internal functions, not in the public installed headers.** Certificates are created by `certtool` with a CA private key (`GetCertificateRequest` → "SteamDatagram_CreateCert" → `SetCertificate`). | `steamnetworkingsockets_certstore.h`; `isteamnetworkingsockets.h` lines 788–816 | source_verified |
| Our measured default: a plain IP loopback connection reports `unencrypted=0`, `unauthenticated=1`. | EXP-013 / `gns_default_connection_security_flags` | locally_measured |

**Consequence.** GNS's own encryption cannot be the authentication layer for this project. Its certificate path needs internal APIs and a CA workflow outside the public interface. And by default it silently accepts anyone.

## 2. Threat model for a two-player lab or LAN session

- **Attacker:** on the same network path or the internet; can read, inject, replay, drop and relay packets; can spoof source addresses.
- **Protect:** who joins a session (only the invited peer); the integrity and confidentiality of game messages; resistance to replaying old messages; no silent downgrade to unauthenticated play.
- **Not in scope yet:** denial of service against the host; traffic analysis; a compromised player PC.

## 3. Options considered

| Option | Verdict | Why |
|---|---|---|
| GNS native certificates with our own CA | **Rejected for now** | Needs internal APIs (`CertStore_*`) and a signing workflow outside the public API. We would be depending on implementation details. |
| App-level "prove you know the key" handshake over GNS's unauthenticated channel | **Rejected** | Not bound to the channel. A man-in-the-middle terminates two GNS sessions and simply relays our handshake. |
| Trust on first use (TOFU) as the only check | **Rejected as sole mechanism** | The first contact is unprotected (ChatGPT answer 1). |
| Self-signed certificates without pinning | **Rejected** | Proves nothing about the peer. |
| Inventing our own cryptographic construction | **Rejected** | Project rule; ChatGPT answer 1. |
| **End-to-end authenticated encryption at our protocol layer, with pinned static public keys, built from a vetted high-level library** | **Recommended** | Independent of the transport, so it works identically on raw UDP and GNS, and it keeps the existing UDP tests meaningful. |

## 4. Recommended design (to be implemented after D-013)

1. **Identity.** Each player has a long-term key pair, generated locally and stored in the user profile, never in the repository. The public-key fingerprint is shown as a short code.
2. **Invitation.** The host creates a session invitation: protocol version, session id, host public-key fingerprint and expiry. It is given to the other player out of band (voice, chat or QR). The two players **confirm each other's fingerprints** before the first join.
3. **Handshake.** Use a **standardised, identity-authenticated handshake with ephemeral keys**, from a reviewed implementation, with both static identities pinned (see §6 for the candidates).
   - **Revised after review #4 (FB-4-03).** libsodium `crypto_kx` is **not** suitable on its own. It is static–static, so it gives no forward secrecy. It also has no transcript binding and no key confirmation, so it is not a specified authenticated handshake. Assembling one around it would be custom protocol design, which is the risk we are avoiding.
4. **Record protection.** Every protocol message after the handshake is AEAD-encrypted, for example XChaCha20-Poly1305, using the derived per-direction keys. Our 22-byte header becomes the associated data. Nonces are per-direction counters, and a sliding window rejects replays and old messages. This replaces, and strengthens, the sequence-based guards that are not security controls today (KI-17).
5. **Session binding.** `ConnectRequest` and reconnect tokens travel only inside the authenticated channel. A reconnect opens a new transport connection and **re-authenticates** before resuming the game session, as ChatGPT answer 2 recommends.
6. **No silent downgrade.** Unauthenticated play is possible only with an explicit `--insecure-loopback` developer flag, and only on 127.0.0.1. On GNS, set `IP_AllowWithoutAuth` deliberately and do not rely on its open-source default.

## 4a. Peer-authentication state machine (FB-4-03)

This specifies the behaviour the chosen handshake must provide. Message formats come from the chosen standard, not from us.

```
           connect()/accept transport peer
 [Idle] ─────────────────────────────────► [Handshaking]
                                              │  timeout 5 s, transcript mismatch, unknown static key,
                                              │  bad signature/MAC, version/suite mismatch
                                              ├──────────────────────────────────────────► [Rejected] (close transport peer)
                                              │  handshake complete AND key confirmation verified both ways
                                              ▼
                                         [Authenticated] ── any record fails AEAD, replay outside window,
                                              │              counter exhausted ─────────────► [Closed]
                                              │  transport Disconnected
                                              ▼
                                         [Closed] ── reconnect = new transport peer + NEW handshake (never resume keys)
```

**Rules:**
- **No protocol messages before authentication.** No message of our game protocol (`ConnectRequest`, tokens, inputs) is processed before `[Authenticated]`. The game session is bound to the authenticated static identity, not to a `PeerId` or an address.
- **Transcript binding.** The handshake transcript hash covers: protocol name and version, cipher suite, both static public keys, both ephemeral keys, the session-invitation id and its expiry, and our `kProtocolVersion`. The derived keys depend on it, so a relay or downgrade changes the keys and the handshake fails.
- **Key confirmation.** Each side proves it holds the derived keys before `[Authenticated]`: the standard's finished or confirmation message, verified on both ends.
- **Records.**
  - Each direction has its own key.
  - The nonce is a 64-bit per-direction counter, starting at 0, strictly increasing, and **never reused**. On exhaustion the connection closes.
  - Our 22-byte header is the associated data.
- **Replay window.** The receiver accepts a counter at most once, within a 1024-record sliding window. Anything older, or already seen, is dropped and counted. Duplicates are not errors on the unreliable channel; they are discarded.
- **Rekey.** Not required for prototype session lengths. Close after 2^32 records per direction.
- **Errors.** A peer that fails any check closes without a detailed error, so it gets no oracle. Locally the failure is counted and logged without key material.

## 5. Tests the implementation must pass (planned)

These cover the requirements of FB-3-01.

- **Negative baseline**, implemented now: plain GNS IP is `unauthenticated=1`, and the open-source build accepts unauthenticated peers by default.
- **Happy path:** the correct pinned peer authenticates on both transports.
- **Wrong identity:** a peer with an unknown or unconfirmed key is rejected before any game message is processed.
- **Man-in-the-middle relay:** a relay that terminates both sides cannot complete the handshake, nor read or modify records.
- **Replay and reorder:** replayed or old records are rejected; tampered records fail AEAD.
- **Reconnect:** re-authentication is required, and an old token alone is not enough.
- **Downgrade:** a client that refuses to authenticate cannot join, except with `--insecure-loopback` on 127.0.0.1.

## 6. Decision needed

**D-013 (user): handshake and crypto library.** Revised after review #4, FB-4-03.

- **(a) TLS 1.3 / DTLS through OpenSSL**, with **mutual authentication by pinned self-signed certificates**: each side checks the peer certificate's SHA-256 fingerprint against the confirmed invitation. This is a standardised and heavily reviewed handshake with forward secrecy, transcript binding and key confirmation built in, and OpenSSL 3.6.5 is already in the GNS build. To verify before choosing:
  - which DTLS version OpenSSL 3.6.5 supports;
  - how to frame TLS over our unreliable channel (DTLS) versus over a reliable channel.
- **(b) The Noise protocol (IK or XX with pinned static keys)** through a reviewed implementation. Smaller and purpose-built. To verify: the implementation's review or audit status and its licence.
- **(c) libsodium `crypto_kx` plus our own handshake.** **Rejected** (FB-4-03): no forward secrecy, no transcript binding, no key confirmation, so it would be custom protocol design.

All options add a dependency to the default build, which today has none. That needs the user's approval.
