# Decision log

Each entry gives the context, the decision, the alternatives considered and how to revisit it.

## D-001: Networking prototype on raw Winsock2 UDP, no third-party dependency (2026-10-23)

- **Context.** Phase 1 needs an auditable two-player prototype. Candidate transports: ENet, Valve GameNetworkingSockets, Steam relay (`ISteamNetworkingMessages`), raw Winsock2.
- **Decision.** Use documented Winsock2 APIs (`socket`, `bind`, `sendto`, `recvfrom`, `ioctlsocket`, `WSAIoctl(SIO_UDP_CONNRESET)`) and our own small protocol of about 1,200 lines of C++ in total.
- **Why.**
  - Zero vendored binaries means nothing undocumented to audit (rule: no undocumented third-party binaries).
  - Every byte on the wire is specified in [protocol.md](protocol.md).
  - Impairment is simulated in-process and deterministically.
  - The scope (one host, two clients, unreliable snapshots plus redundant inputs) needs no reliable ordered channel yet.
- **Alternatives.**
  - ENet is a good next step if a reliable channel is needed (MIT, small).
  - GameNetworkingSockets or Steam relay would add NAT traversal. Steam relay on AppID 480 has unverified terms (F-COOP-006).
  - All are deferred, not rejected.
- **Revisit when** Phase 2+ needs internet play across NAT, reliable ordered messages (mission events), or encryption.

## D-002: Host-authoritative, results-only replication model (2026-10-23)

- **Decision.** The host owns all simulated state. Clients send inputs only and apply snapshots. Remote entities are interpolated; the local entity is predicted and reconciled.
- **Why.** This matches the default model the coop-anything playbooks recommend for closed native games (F-COOP-002, 003, 005). Lockstep is rejected: it needs engine determinism that nobody has shown for Shadow of War.
- **Consequence.** A future game integration would need the host's game to be the single authority over combat, AI and Nemesis results. Clients would apply results, never re-roll them (F-COOP-012 suggests a single global LCG, which is unverified).

## D-003: No static analysis of the on-disk executable in Phase 1 (2026-10-23)

- **Context.** Static analysis of the exe has two problems:
  - The entry point is in a high-entropy `.bind` section, and third parties report SteamStub plus Denuvo (F-ENV-003/004, F-SOWM-004, F-COMM-014).
  - The public workflow that makes static analysis practical (SoWMods) unpacks the exe with Steamless (F-SOWM-009). That removes SteamStub.
- **Decision.**
  - Do not unpack, unwrap, dump or otherwise analyse the protection layer.
  - A planned section-content probe was stopped before it ran (EXP-003).
  - Ghidra analysis is deferred.
- **Why.** This is required by rule 3. The EULA also prohibits reverse engineering (F-COMM-015), so even lawful-interoperability arguments need a decision from the user, and ideally legal advice, first.
- **Revisit when:**
  - (a) the user decides on the legal position, for example EU Directive 2009/24/EC Article 6 interoperability analysis, where applicable; and
  - (b) a target build exists without the protection conflict. Third-party sources say the GOG release is DRM-free (F-COMM-017). That is unverified.

## D-004: Do not install Ghidra or x64dbg yet (2026-10-23)

- **Decision.** Do not ask the user to install RE tooling in Phase 1.
- **Why.** Given D-003, there is no lawful target for them yet. Rule 9 requires approval for installs.
- **Revisit** together with D-003.

## D-005: Third-party build-specific data is hypothesis-only (2026-10-23)

- **Decision.** RVAs and offsets from SoWMods and coop-anything are recorded as HYPOTHESIS and are never compiled into Project Mordor code.
- **Why.**
  - SoWMods has no build check (F-SOWM-005).
  - coop-anything claims our hash but publishes no code.
  - Neither licenses those artefacts for reuse (F-SOWM-017).

## D-009: GitHub-based collaboration with ChatGPT as reviewer (2026-10-23)

- **Decision.** Use `.ai-collaboration/` in the repository as the shared channel, with GitHub as the transport.
- **Branches.** Work happens on feature branches. Pull requests are never merged automatically.
- **Feedback.** ChatGPT feedback is advice. It is verified and triaged in `CHATGPT_FEEDBACK.md`, and it never overrides the project rules.
- **Reporting.** The status script (`tools/collab/report.py`) runs only on demand. It has no network access and only reads local git and evidence files.

## D-013: Crypto library for authenticated peers (PENDING, user decision)

- **Context.** Review #3 FB-3-01 and answer 1. In GNS 1.6.1 open-source, unauthenticated peers are accepted by default, and certificate trust is internal-only (`docs/authentication-design.md` §1).
- **Proposal (revised after review #4 FB-4-03).** A standardised handshake with forward secrecy, transcript binding and key confirmation, via a reviewed implementation:
  - **TLS 1.3/DTLS through OpenSSL with pinned self-signed certificates**; or
  - **Noise IK/XX**.

  libsodium `crypto_kx` plus our own handshake is rejected. Full state machine: `docs/authentication-design.md` §4a.
- **Status.** Awaiting the user's approval to add the dependency.
- **Evaluation (2026-10-23, after the GNS adapter).** [d013-evaluation.md](d013-evaluation.md) recommends **DTLS 1.2 via OpenSSL 3.6.5 with RFC 7250 raw public keys pinned by fingerprint**, carried over the Transport's `Unreliable` class on both backends, with the invitation id and protocol version bound through a keying-material exporter. Fallback: Noise via snow. No dependency has been added; until authentication is implemented and independently reviewed, the prototype stays on loopback or explicitly trusted lab networks.
- **Proof test (2026-10-24, owner-approved).** DTLS 1.2 + RFC 7250 pinned raw public keys work in OpenSSL 3.6.5. Every failure path tested (wrong key either way, no RPK, DTLS 1.0, non-AEAD suite, replay, tamper) is rejected. The exporter binding is fresh per handshake. Details: [d013-evaluation.md](d013-evaluation.md#proof-test-review-5-fb-5-05-2026-10-24), EXP-017. OpenSSL is now a direct dependency of the optional GNS build. **Still open:** integration behind the Transport and an independent review (KI-29).
- **Over a real transport (2026-10-24, review #6).** `DtlsSession` runs over `Transport::Unreliable` on both backends, with OpenSSL retransmission, cookies and the fail-closed invitation binding: 9 transport tests × 2 backends (EXP-018). GNS reliable delivery is not used for DTLS (decided: datagram protocol, one path for both backends). **Still open:** carrying game messages, a cookie before per-peer state, and an independent review.
- **Executables (2026-10-24, review #8).** Game traffic of the real executables runs over DTLS with provisioned keys (D-016) and a secure reliable channel (EXP-021). **Still open:** a stateless cookie (KI-30), an independent review, and a LAN test. Untrusted networks remain off limits.

## D-017: Application-level reliable events (2026-10-24, review #9 FB-9-02, KI-28)

- **Context.** A transport acknowledgement is not proof that the game applied an event. Nothing replayed events after a reconnect.
- **Decision.** Protocol v2 adds `Event` and `EventAck` (see protocol.md). Ids are per player and session. The client application acks after applying. The host keeps, resends and replays until it is acked.
  - Application-level reliability never depends on transport reliability: the host resends on reliable transports too.
  - Without that, an event that overtook the `ConnectAccept` and was discarded stalled the stream for a whole soak run (EXP-022).
  - `EventAck` is unreliable (cumulative, idempotent, resent with each heartbeat), so it never blocks other reliable data.
- **Limits.**
  - Not durable across a host or client process restart; a new session resets the stream.
  - Window of 64 unapplied events per player.
- **Revisit** if events must survive a host restart (persistence) or exceed the window.

- **Amended after review #10 (FB-10-01/02/04, 2026-10-24).**
  - The host accepts an `EventAck` only up to the highest id the transport accepted.
  - An event counts as sent only on `Sent`.
  - **Protocol v3** adds an explicit leave handshake (`DisconnectAck`); the host keeps the transport peer open for 1.5 s (KI-37).
  - Events are still not durable (KI-28b).

## D-016: Identity provisioning for the executables (2026-10-24, review #8 FB-8-03 / answer 3)

- **Decision.**
  - **Keys:** `mordor-keygen --new FILE` writes a P-256 key as `MRDRKEY1` plus a DPAPI blob (current-user scope), with a **protected DACL** granting only the current user. Loading refuses broader permissions, foreign or corrupt files, and a blob from another Windows user.
  - **Invitations:** `mrdr1:<id>:<protocol>:<expiry>:<host SPKI base64>`, public fields only. Players confirm fingerprints out of band.
  - **Host:** `--secure-key FILE --invitation STR --allow-peer B64[,B64]`.
  - **Client:** `--secure-key FILE --invitation STR`.
  - **Fail closed:** any partial, mismatched, expired or unreadable setup exits with code 2.
  - **Secrecy:** private keys never appear on command lines, in environment variables, logs, invitations or Git.
- **Alternatives.**
  - Windows certificate store: heavier, and pulls in X.509.
  - Passphrase-encrypted PEM: needs a passphrase prompt or argument, and an argument would land on the command line.
  - Keys in environment variables: rejected by answer 3.
- **Consequence.** Keys are bound to the Windows user (KI-33).

## D-015: Security as a Transport decorator, with no plaintext fallback (2026-10-24, review #7 answer 3)

- **Context.** DTLS worked over the Transport (EXP-018), but Host/Client still spoke plaintext, and the reconnect and pending rules lived only in test code (FB-7-05).
- **Decision.**
  - `SecureTransport` wraps any backend and implements `Transport`. Host and Client opt in through `secure` in their config.
  - If security is configured but cannot be provided, `start()` fails. There is never a silent plaintext fallback.
  - Pre-authentication state is bounded (FB-7-01).
  - A same-address reconnect is promoted only after it authenticates.
  - The DTLS retransmission back-off is 250 ms doubling to a 2 s cap, set through OpenSSL's `DTLS_set_timer_cb` (FB-7-02).
- **Alternatives.**
  - Calling `DtlsSession` from inside Host/Client: this mixes session logic with security and makes a plaintext path easy to keep by accident.
  - A new UDP association per reconnect: possible, but it needs client-side socket churn. Kept as an option.
- **Revisit** before any untrusted network: a stateless cookie gate, key/invitation provisioning for the executables, and an independent review.
- **Amended after review #10 (answer 5, 2026-10-24).** The cookie gate is now stateless: one `DTLSv1_listen` listener, with no per-peer state before a valid cookie. Cookie-proven ClientHellos are exempt from the 64/s budget (EXP-023). Still to do before an untrusted network: an independent review.
- **Amended after review #11 (FB-11-01, 2026-10-24).** Over a connection-oriented inner transport (GNS), the client's DTLS handshake starts only when the inner transport reports `Connected`. An inner failure before that is reported to the caller as `inner_connect_failed`. GNS peers still pass the same cookie admission: that a GNS connection proves return routability is not verified from source.

## D-014: On connection-oriented transports, the transport owns connection establishment (2026-10-23)

- **Context.** EXP-016: under 60% loss the client's own 5 s connect timer raced GNS's handshake timeout, and `heavy_loss_and_duplication_never_crash` passed only 3/8 on GNS.
- **Decision.**
  - On backends with `reports_disconnects`, the client's connect window starts at the transport `Connected` event.
  - A transport `Disconnected` before that is a handshake failure (`Failed`, reason `transport_handshake_failed`).
  - Every reconnect opens a new transport connection; a restarted host does not know the old one (review #3 answer 2).
  - GNS gets `TimeoutInitial` 10 s and `TimeoutConnected` 3 s.
- **Result.** 8/8 on GNS; UDP behaviour unchanged (it reports no transport events, so the old timers apply).
- **Revisit** if a backend's establishment timeout is not configurable.
- **Amended after review #5 (FB-5-02, 2026-10-24).** The session layer also keeps an overall bound, so the client never relies on the transport alone to report failure.

  | Phase | Bound | Owner |
  |---|---|---|
  | Transport establishment | GNS `TimeoutInitial` 10 s | transport |
  | Session handshake | `connect_timeout_us` 5 s from transport `Connected` (UDP: from `connect()`) | session |
  | Whole connect operation | `connect_deadline_us` 20 s from `connect()` (`Failed`, reason `connect_deadline`) | session |
  | Reconnect | `reconnect_window_us` 15 s from the start of the reconnect, never reset by transport retries | session |
  | Connected liveness | GNS `TimeoutConnected` 3 s / our 3 s timeout | both, equal |
  | Host slot reservation | grace 30 s (KI-11) | host |

  Tests: `connect_to_absent_host_fails_within_deadline`, `black_holed_connect_hits_overall_deadline`, `host_gone_during_handshake_fails_within_deadline`, `repeated_outages_reconnect_each_time_with_same_identity`.
- **Policy (review #6 answer 3, 2026-10-24).** `connect_deadline_us` is configurable within an explicit range of [1 s, 120 s]: `--connect-deadline-s` on the client, default 20.
  - Out of range is an error, never a silent clamp, in both the CLI and `Client::start`.
  - Test: `connect_deadline_policy_is_bounded`.

## D-012: GameNetworkingSockets for the transport, behind an interface (2026-10-23, user decision)

- **Context.** Review #2 (answer 4) recommended a proven library over a custom reliability protocol. The project also needs authenticated and encrypted transport before any WAN use (KI-04, KI-17).
- **Options weighed:**
  - ENet (MIT; small; no security).
  - GameNetworkingSockets (BSD-3; reliability, fragmentation, congestion control and encryption built in; heavier: OpenSSL and protobuf).
  - The in-house `ReliableChannel` (no dependencies; we own all the edge cases).
- **Decision (the user chose GNS):**
  - Adopt GameNetworkingSockets as the target transport, behind a small `Transport` interface.
  - Build it through vcpkg as an **optional** CMake feature (`MORDOR_WITH_GNS`), so the default build stays dependency-free.
  - Use only the standalone open-source API: no Steamworks, no relay, no Steam AppID.
- **The in-house channel** (local WIP branch `feature/reliable-events`) is not discarded. It becomes the reference behaviour, and the event-order, deduplication, reconnect and fault-injection tests run against both.
- **Impairment per backend (review #3 FB-3-06, 2026-10-23).** Raw UDP keeps `ImpairedSender`; the GNS adapter uses GNS's native fault injection. Both use the same target profiles, log realized conditions, and get transport-labelled gate versions.
- **Liveness on GNS (review #3 answer 2).** GNS owns transport liveness and reconnect; game session identity stays in the application.
- **Revisit if** GNS fails the same tests, its build cost proves unacceptable, or its default security does not meet the WAN prerequisite. The spike measured it (EXP-013): reliable and ordered delivery under loss passes, and connections are encrypted by default but **not authenticated**.

## D-010: Bounded input backlog by shedding; separate reliable event plane later (2026-10-23, review #1)

- **Decision (FB-1-01).** The host simulates exactly one timestep per entity per tick. It sheds input backlog beyond 2 queued commands, oldest first.
- **Rejected alternative:** catch-up by simulating extra timesteps in one tick. That was the previous behaviour, and it lets entity time run ahead of world time. That matters for future collision, combat timing and AI.
- **Deferred alternative:** client-side input-rate adaptation (time dilation), which shrinks the backlog without shedding.
- **Decision (FB-1-04).** Keep movement on unreliable sequenced snapshots plus redundant inputs. Design a separate reliable, ordered, idempotent event channel (unique event ids, deduplication, resync from snapshot) before any combat or Nemesis work. Evaluate ENet or GameNetworkingSockets behind a transport interface. Deferred to Phase 2A P2.

## D-006: Loopback by default; LAN by explicit opt-in (2026-10-23)

- **Decision.**
  - `mordor-host` binds `127.0.0.1` unless `--bind` is given.
  - Clients bind loopback when the host is on loopback.
  - No firewall rules are created.
- **Why.** Rule 9: changing the firewall needs approval. The LAN test is NOT_RUN (EXP-007).
