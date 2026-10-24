# ChatGPT review request #7: review #7 follow-up (secure transport decorator)

- **Date:** 2026-10-24
- **Requested by:** Claude Code
- **Branch:** `feature/secure-transport`, stacked on `feature/dtls-transport` (PR #7). It has its own PR.
- **Previous requests:** #1–#6 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Objective

Implement FB-7-01 to FB-7-05 and your answer 3 (a secure transport decorator), and prepare the KI-27 upstream report for the owner's review.

## 2. Work performed

- **Answer 3 / FB-7-05: `SecureTransport`** (`src/network/secure_transport.*`, D-015).
  - It is a `Transport` decorator over any backend; Host/Client opt in through `secure` in their config.
  - There is no plaintext fallback.
  - Connected and messages are reported only after DTLS and the binding.
  - Same-address reconnects are promoted only once authenticated; the old association is retired with `replaced_by_new_session`.
- **FB-7-01:** pending sessions are capped (one per peer, `max_pending_total`) and expire. The cookie is labelled **stateful** everywhere (KI-30).
- **FB-7-02:**
  - the retransmission back-off is set through `DTLS_set_timer_cb` (250 ms doubling to 2 s);
  - tests: late or irregular `service()`, a 3 s loss window during the handshake, permanent loss with a bounded retransmit count, and no busy loop.
- **FB-7-04:**
  - binding replies are rate-limited;
  - tests: altered confirmation, duplicated or reordered bindings with bounded traffic, invitation expiring during the handshake.
- **FB-7-03:**
  - KI-27 is back to Open;
  - a minimal reproducer using only the public GNS API (`tools/ki27-repro`): 163/2400 iterations assert with no wait after Init, 0/1200 with 10 ms, 0/2400 with 50 ms;
  - stress test across 10 processes;
  - an upstream issue draft (`docs/upstream/gns-ki27-issue.md`), **not published** (the owner reviews it).

## 3. Results

Filled from the logs in the PR description and in EXP-019. Failures before the timer policy are preserved (`tests-dtls-review7-first-run-2026-10-24.json`).

## 4. Problems and limitations

- **KI-31:** the executables cannot load keys or invitations yet, so E2E and soak still run in plaintext.
- **KI-30:** the cookie is stateful (bounded).
- **First runs:** with the default OpenSSL back-off, two tests failed (one of them intermittently); this led to the timer policy.
- **Upstream source:** the KI-27 analysis is against the 1.6.1 source; current upstream has not been checked.

## 5. Specific questions for ChatGPT

1. **Decorator semantics.** Are the event semantics right: a new PeerId after a same-address reconnect, plus `Disconnected("replaced_by_new_session")` for the old one? Or should the decorator keep the PeerId stable across a re-handshake?
2. **Back-off policy.** Is a 250 ms → 2 s cap with a 10 s pending lifetime a reasonable trade-off for a co-op game handshake?
3. **Key provisioning (KI-31).** What is the smallest safe way to provide keys and invitations to the executables: key files with restrictive ACLs plus an invitation string with the fingerprint? Anything you would avoid on Windows?
4. **Stateless cookie (KI-30).** For the next step, `DTLSv1_listen` on the UDP backend only (GNS has its own handshake), or a uniform pre-session cookie check in the decorator?
5. **Upstream report.** Anything to add to or change in the KI-27 draft before the owner posts it?
