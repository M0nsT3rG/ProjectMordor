# ChatGPT review request #5: review #5 follow-up (GNS hardening + DTLS proof)

- **Date:** 2026-10-24
- **Requested by:** Claude Code
- **Branch:** `feature/gns-hardening`, stacked on `feature/gns-adapter` (PR #5). It has its own PR.
- **Previous requests:** #1–#4 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Objective

Implement FB-5-01 to FB-5-04, and the FB-5-05 proof test that the owner approved: DTLS 1.2 + RFC 7250, using the OpenSSL already installed by vcpkg.

## 2. Work performed

- **FB-5-01:**
  - counters documented as wire-level diagnostics;
  - `discarded_on_close` is per message on every backend (GNS leaves it at 0); GNS per-connection losses go to `closed_with_unacked_reliable`;
  - new test `contract_mid_backlog_disconnect_then_reconnect_is_clean`.
- **FB-5-02:**
  - overall `connect_deadline_us` (20 s) at the session layer;
  - timeout-ownership table in D-014;
  - 4 lifecycle tests on both backends.
- **FB-5-03:** single effective GNS impairment config.
  - A different request is refused (`set_impairment` returns false, `impairment_refused` is counted), and `open()` with a conflict fails.
  - The same request joins; the config is reset only by its last holder.
  - Guard and leakage test added.
- **FB-5-04:**
  - 8 `_over_transport` substitute tests that run our protocol rules through a real connection on every backend;
  - `docs/test-coverage.md` gives the reason and the substitute for each skip.
- **FB-5-05:** `tests/dtls/dtls_proof_tests.cpp`, 8 cases, in-memory datagrams. OpenSSL is now a direct dependency of the optional GNS build.

## 3. Results

Filled from the logs in the PR description and in EXP-017.

## 4. Problems and limitations

- **A false GNS pass found.** `timeout_notice_recovery_under_jitter_reconnects_once` "passed" on GNS only because the client's cut silently replaced the host's jitter for the whole process. With the guard it would be refused, so it is now UDP-only, with the reason.
- **DTLS first run failed.** All 8 cases failed: `SSL_add_expected_rpk` needs DANE enabled first. The failed log is preserved.
- **KI-27.** An intermittent GNS internal assertion (`socketthread.cpp(2609)`) appears in contract runs. It is pre-existing: it appears on `9ae2b2a` too. Not investigated yet.
- **Application-level delivery** (event ids, acks, replay) is design-only (KI-28).
- **DTLS integration** over the Transport is not done (KI-29).

## 5. Specific questions for ChatGPT

1. **Impairment guard.** "Same config joins, different config is refused, last holder resets": is that the right rule? Or should a second holder be refused even with an identical config?
2. **Counter semantics.** Is it acceptable to keep `discarded_on_close` per message only (0 on GNS) and count connections separately? Or should the contract expose a separate "unknown count" flag?
3. **Deadline.** Is a 20 s overall connect deadline reasonable for a co-op join flow, and should it be user-configurable?
4. **DTLS next step.** For the integration, would you do the handshake over the existing `Unreliable` class with our own retransmission driven by `DTLSv1_handle_timeout`? Or should DTLS ride on GNS reliable messages first, as the simpler path?
5. **Cookies.** Is `DTLSv1_listen` cookie exchange needed on GNS (which already has its own handshake), or only on the raw UDP backend?
6. **KI-27.** Do you know this GNS assertion during teardown, and is it safe to ignore in a release build?
