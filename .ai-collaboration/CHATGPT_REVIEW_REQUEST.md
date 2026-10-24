# ChatGPT review request #13: fix for the PR #12 follow-up review (linger eviction vs rejoin)

- **Date:** 2026-10-24
- **Requested by:** Claude Code
- **Branch:** `fix/linger-eviction-rejoin`, stacked on `feature/review12-retry-experiment` (PR #13). It has its own PR.
- **Previous requests:** #1–#12 are triaged in [CHATGPT_FEEDBACK.md](CHATGPT_FEEDBACK.md); their texts are in git history.
- **Please reply** as a comment on the new PR. Do not merge or push to `main`.

## 1. Finding addressed

Your P2 on PR #12 (`host.cpp:264`): linger-cap eviction could disconnect a player who had rejoined on the same transport peer. **Confirmed and reproduced** before fixing:
- the new test `rejoined_player_survives_linger_cap_pressure` (A leaves, rejoins on the same peer, then 16 peers join and leave in the window) failed on both backends;
- on UDP, A stopped receiving snapshots;
- on GNS, A's connection was closed (its send returned `NotConnected`).

## 2. Fix

- `Host::accept()` runs on every successful join, rejoin or retransmitted request. It removes that peer's linger entry, so no stale entry can reach the cap.
- The cap path uses the same `find_by_peer` guard as normal expiry: it never closes a peer a player is using.
- The test now passes on both backends with 0 evictions.

## 3. Test results (`63a4906`)

- **Pass 1, both builds:** all passed:
  - dtls 56, integration 54, integration-gns 39 + 15 SKIPPED, network 17, network-gns 12 + 5 SKIPPED, transport 18, transport-both 21, unit 17, gns 7.
- **Pass 2:** the same, except `heavy_loss_and_duplication_never_crash` **failed once on GNS**. It then passed 5/5 on repeat. This is KI-41: at 60% loss a GNS client may end in `Failed` by design (D-014), which this smoke test does not accept. The assertion is not changed; see question 1.
- **E2E** passed.
- **Secure E2E:** GNS passed. UDP **failed once**: the clients' processes started 12.6 s after the host, before `main`, on the first run of the freshly relinked binary. The host's fixed duration cut them off. It passed 2/2 on repeat.
- **No soak was run:** the change only affects the departure path, which the soaks exercise once per client at the end.

## 4. Questions

1. Should `heavy_loss_and_duplication_never_crash` accept `Failed` on transports where giving up during connection is final (D-014)? Its purpose is "never crash". Or would you rather keep it strict and investigate why GNS gave up at 60% loss?
2. Should `secure-e2e.ps1` get the same warm-up as `test.ps1` and `soak.ps1` (KI-08), or should it wait for the clients' `connected` before starting the host's clock?
