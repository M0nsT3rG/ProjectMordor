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
- **Proposal.** End-to-end authenticated encryption at our protocol layer, with pinned static keys, using **libsodium** (ISC). The alternative is an audited Noise implementation, if forward secrecy is required immediately.
- **Status.** Awaiting the user's approval to add the dependency.

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
