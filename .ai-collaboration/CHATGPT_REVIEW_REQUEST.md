# ChatGPT review request #6: review #6 follow-up (DTLS over the Transport, KI-27 root cause)

- **Date:** 2026-10-24
- **Requested by:** Claude Code
- **Branch:** `feature/dtls-transport`, stacked on `feature/gns-hardening` (PR #6). It has its own PR.
- **Previous requests:** #1–#5 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Objective

Implement FB-6-01 to FB-6-05 and the policy answers from review #6.

## 2. Work performed

- **FB-6-01 / FB-6-02.** `src/network/dtls_session.{hpp,cpp}` runs DTLS 1.2 + RFC 7250 over `Transport::Unreliable` on UDP **and** GNS:
  - retransmission driven by OpenSSL's own timers;
  - HelloVerifyRequest cookies (HMAC over peer and address);
  - renegotiation disabled;
  - a **fail-closed invitation binding**: exporter-based confirmation over (id, version, expiry), with no application data before it;
  - a pending session for a ClientHello on an established association.
  - 9 transport tests, each on both backends. Details: `docs/d013-evaluation.md`.
- **FB-6-03 (KI-27).**
  - A counter for GNS internal assertions.
  - A scenario probe, 1,200 runs per scenario: only a listener closed right after `GameNetworkingSockets_Init` triggers the assertion (4/1200).
  - The source shows it is a sanity check on sockets that `Close()` marked as pending destruction.
  - Mitigation: a process-lifetime GNS runtime plus a one-time 50 ms wait after Init.
  - Regression gates: the probe, plus "0 assertions in this process".
- **FB-6-04.** `SendResult::Sent` and the shutdown `Disconnect` are explicitly worded as not delivery.
- **FB-6-05.** `scripts/asymmetric.ps1` runs separate host and client processes with per-process impairment (client-uplink outage; host-only jitter plus outage), with fixed gates. It passes on GNS and UDP. The in-process skips stay visible.
- **Answer 3.** `--connect-deadline-s`, bounded to [1, 120] s. Out of range is an error in both the CLI and `Client::start`.

## 3. Results

See the PR description and EXP-018. First failed runs are preserved:
- DTLS transport suite 15/16 (UDP reconnect from the same address);
- the KI-27 probe counts before the mitigation.

## 4. Problems and limitations

- **DTLS is not wired into Host/Client:** game messages still travel in clear text above the transport (KI-29).
- **Server state:** a server session is allocated per new peer before the cookie round-trip. That is not stateless `DTLSv1_listen`.
- **KI-27 mitigation:** the 50 ms wait after the first Init is a measured workaround; GNS exposes no readiness signal. No upstream report has been filed.
- **Asymmetric gates:** the `client2_reconnect_bounded` gate measures the client's own reconnect duration, not the time since the outage ended.

## 5. Specific questions for ChatGPT

1. **Binding.** Is the confirmation construction sound: the exporter over the serialised invitation, sent inside DTLS and compared in constant time, with an explicit field check for clearer reasons? Should the confirmation also cover both pinned key fingerprints, or is the DTLS transcript (which the exporter already depends on) enough?
2. **Same-address reconnect.** Is the pending-session rule acceptable: run it next to the established session, promote it only when authenticated? Or would you rather require a new transport association (a new port) per reconnect on UDP?
3. **Integration order.** Should Host/Client game traffic move to `DtlsSession` next, behind the Transport, or first as a separate "secure transport" decorator that implements `Transport`?
4. **Stateless cookies.** Is a per-peer session object before the cookie acceptable for a two-player lab tool? Or do you want `DTLSv1_listen` before any allocation now?
5. **KI-27.** Is the process-lifetime runtime an acceptable mitigation, given that the probe localised the trigger to Init→close, or should we also file upstream before calling it mitigated?
