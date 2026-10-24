# ChatGPT review request #9: review #9 follow-up (liveness, realized-impairment gates, application events)

- **Date:** 2026-10-24
- **Requested by:** Claude Code
- **Branch:** `feature/secure-liveness`, stacked on `feature/secure-provisioning` (PR #9). It has its own PR.
- **Previous requests:** #1–#8 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Objective

Implement FB-9-01 to FB-9-05, including FB-9-02 (application-level reliable events, KI-28), on the owner's instruction.

## 2. Work performed

- **FB-9-01.** Liveness is refreshed only by an authenticated, decrypted record of the established session. The regression injects garbage and replayed records from the vanished peer's address; the mutation check kills the old behaviour.
- **FB-9-02 (D-017, protocol v2).**
  - `Event` / `EventAck`: per-player contiguous ids, acked only after the client application applied them.
  - The host keeps unapplied events (window 64), resends them on every transport and replays them on reconnect; a new session resets the stream.
  - Tests: exactly-once in-order under loss, duplication and jitter on both backends; outage and reconnect; ack-after-apply versus transport ack; host restart; window; secure re-authentication.
  - The executables send and verify events (`--event-interval-ms`); soak v5 gates them.
- **FB-9-03.** Soak gate v5 (fixed before its runs; v1–v4 thresholds unchanged) checks the **realized** impairment:
  - min RTT ≥ 2 × latency − 2 ms;
  - handshake delay ≥ 1 RTT (plaintext) or 3 RTT (secure);
  - upstream delivery ratio within [1 − 2·loss − 0.02, 1 − loss/2 + dup].

  It also checks the event gates.
- **FB-9-04.** The key file is opened once (`FILE_FLAG_OPEN_REPARSE_POINT`; reparse points refused), its DACL is checked on that handle, and it is read from the same handle. Writes go to a temporary sibling and are then moved into place without replacing. Tests: interrupted write, truncated key, leftovers. **The symlink case was not run** (no symlink privilege, KI-36).
- **FB-9-05.**
  - Misuse tests on both backends: conflicting, valid-but-not-allowed, spoofed host key, swapped identity, stale.
  - A rate limit on unauthenticated ClientHellos (per second and per address), with accounting. A flood test checks that the player stays responsive.

## 3. Defects found while doing it (fixed, with regression tests)

1. **The first version of the rate limit let forgeries from a player's own address starve its reconnect.** Known players are now exempt; the FB-8-01 cookie rules govern them.
2. **Head-of-line blocking.** Frequent reliable `EventAck`s delayed the final reliable `Disconnect` past the close drain, so the player was timed out. `EventAck` is now unreliable, and `Disconnect` is also sent unreliably.
3. **An event that overtook the `ConnectAccept` was discarded by the client and never resent on reliable transports**, so a player got 0 events for the whole run. The host now resends on every transport.

## 4. Problems and limitations

- **KI-35 (open, root cause not established):**
  - Secure UDP harsh stall recovery has a tail above the 3 s gate.
  - Measured in quick runs: 3 of 8 above 3 s with the new 1 s DTLS retransmit cap, against 7 of 12 before.
  - The gate is unchanged, and the full soak results report it as is.
- **KI-30:** the cookie is still stateful.
- **KI-32:** cumulative acks only.
- **KI-36:** the symlink check was not run here.
- **Events are not durable** across process restarts.

## 5. Specific questions for ChatGPT

1. **Events.** Is "ack after application apply + host resend on every transport + replay on reconnect + reset on new session" a sound semantic for consequential co-op events, or do you want persistence (a host-side log) before calling KI-28 done?
2. **Realized-impairment gates.** Are min-RTT, handshake-delay and delivery-ratio bounds the right realized checks, or would you gate directly on transport counters (`simulated_drops`, which GNS cannot count)?
3. **KI-35.** Do you see a mechanism for the bimodal secure reconnect latency under 15% loss (most runs about 1.3–2.5 s, some 4–11 s, in separate processes only)? What would you instrument next?
4. **Rate limiting.** Is exempting addresses with an established session the right trade-off, given that the FB-8-01 cookie rules already govern them?
