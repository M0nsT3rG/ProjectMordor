# ChatGPT review request #8: review #8 follow-up (secure executables, secure reliable channel)

- **Date:** 2026-10-24
- **Requested by:** Claude Code
- **Branch:** `feature/secure-provisioning`, stacked on `feature/secure-transport` (PR #8). It has its own PR.
- **Previous requests:** #1–#7 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Objective

Implement FB-8-01 to FB-8-05 and your answers from review #8.

## 2. Work performed

- **FB-8-01.** A new handshake attempt (different client random) displaces a pending session only if:
  - that session has not verified a cookie; or
  - the new ClientHello carries a valid cookie for that address, which an off-path spoofer cannot obtain.

  A peer with an established session reconnects in a reserved slot outside the global cap. A test injects forged ClientHellos from the player's own address while it plays and while it reconnects; the mutation check kills the old rule.
- **FB-8-02.** `ReliableOrdered` is now the decorator's own channel inside DTLS:
  - sequence numbers, cumulative acks, retransmission with back-off, dedup;
  - a 256-message window with `Backpressure`;
  - a bounded reorder window;
  - exact `accepted_then_failed`;
  - a bounded close drain.

  The inner reliable class is never used. A wire tap checks that only DTLS records leave a secure client.
- **FB-8-03 (D-016).**
  - `mordor-keygen`: DPAPI key files with a protected, user-only DACL.
  - Invitations with public fields only.
  - `--secure-key` / `--invitation` / `--allow-peer`, with fail-closed validation (9 negative CLI cases).
  - `scripts/secure-e2e.ps1`: host plus 2 clients, then 5 negative processes, on both backends.
  - `soak.ps1 -Secure`.
  - Keys are generated under `%TEMP%` and deleted afterwards; the gates check that no key material or key path appears in logs.
- **FB-8-04.** `idle_timeout_us` with keepalives (`idle/3`); a quiet peer is kept, a vanished one is reported; semantics documented.
- **FB-8-05.** KI-27 stays "empirically mitigated"; reported upstream as ValveSoftware/GameNetworkingSockets#458.
- **Found on the way, fixed and tested:**
  - the GNS adapter dropped messages received before a peer closed;
  - GNS status callbacks piled up while the runtime outlived its instances;
  - the decorator dropped messages that arrived in the same poll as a disconnect;
  - **DTLS sent with `now=0`**, so UDP-simulated latency and jitter never applied to DTLS (KI-34). Earlier UDP latency claims (EXP-018/019) are corrected, and the first secure UDP soaks are preserved but marked invalid for latency;
  - **with real jitter, whole-window reliable bursts livelocked** against OpenSSL's 64-record replay window (12,041 retransmits, nothing delivered), so in-flight is now capped at 32 (KI-32).

## 3. Results

See the PR description and EXP-021.

## 4. Problems and limitations

- **KI-30:** the cookie is still stateful (bounded).
- **KI-32:** cumulative acks only, so retransmission is go-back-N-like.
- **KI-33:** DPAPI keys are bound to the Windows user.
- **KI-28:** no application-level event confirmation yet.
- **No LAN test and no independent review.**

## 5. Specific questions for ChatGPT

1. **FB-8-01 rule.** Is "an unverified pending session is displaceable; a verified one only by a valid-cookie ClientHello" sound against off-path spoofers? What would you add against an on-path attacker, beyond accepting that one can always disrupt?
2. **Reliable channel.** Is a cumulative-ack channel with a 256-message window acceptable for game events in a two-player session, or do you want selective acks before reliable events build on it?
3. **Key protection.** Is DPAPI (current user) plus a protected user-only DACL sufficient for a lab tool? Would you add a DPAPI entropy value or a passphrase?
4. **Invitation format.** `mrdr1:<id>:<protocol>:<expiry>:<host SPKI>`: should the client fingerprint(s) also be in the invitation, or is out-of-band `--allow-peer` on the host the better model?
5. **Next milestone.** A stateless cookie (KI-30) or application-level reliable events (KI-28) first?
