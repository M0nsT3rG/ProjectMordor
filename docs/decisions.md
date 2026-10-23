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

## D-006: Loopback by default; LAN by explicit opt-in (2026-10-23)

- **Decision.**
  - `mordor-host` binds `127.0.0.1` unless `--bind` is given.
  - Clients bind loopback when the host is on loopback.
  - No firewall rules are created.
- **Why.** Rule 9: changing the firewall needs approval. The LAN test is NOT_RUN (EXP-007).
