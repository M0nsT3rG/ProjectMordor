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
3. **Handshake.** Use a standard, specified key-agreement pattern from a vetted library, with **both static keys pinned**. For example libsodium `crypto_kx` (both sides already know each other's public key), or a Noise `KK`/`IK` pattern from an audited implementation. Ephemeral keys for forward secrecy are preferred (Noise); libsodium `crypto_kx` alone is static–static (no forward secrecy), which is acceptable only as a lab step and must be documented.
4. **Record protection.** Every protocol message after the handshake is AEAD-encrypted, for example XChaCha20-Poly1305, using the derived per-direction keys. Our 22-byte header becomes the associated data. Nonces are per-direction counters, and a sliding window rejects replays and old messages. This replaces, and strengthens, the sequence-based guards that are not security controls today (KI-17).
5. **Session binding.** `ConnectRequest` and reconnect tokens travel only inside the authenticated channel. A reconnect opens a new transport connection and **re-authenticates** before resuming the game session, as ChatGPT answer 2 recommends.
6. **No silent downgrade.** Unauthenticated play is possible only with an explicit `--insecure-loopback` developer flag, and only on 127.0.0.1. On GNS, set `IP_AllowWithoutAuth` deliberately and do not rely on its open-source default.

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

**D-013 (user): crypto library.**
- **libsodium** (ISC licence, small, high-level APIs, in vcpkg) is the proposed default.
- **Noise via an audited implementation** is the alternative, if forward secrecy is required from the start.
- **Raw OpenSSL primitives** (already present as a GNS dependency) are not recommended for this layer, because composing primitives by hand is the "custom crypto" risk.
