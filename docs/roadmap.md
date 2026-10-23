# Roadmap

## Phase 1: Research and feasibility (this phase), complete with a conditional result

- [x] Environment and game build inventory (read-only, sanitized)
- [x] Prior-art review with commit-pinned evidence (3 repos requested plus 9 related)
- [x] Evidence system with a validator and index generator
- [x] Architecture unknowns, subsystem notes, feasibility matrix, risk register
- [x] Independent networking prototype (host, client, protocol, 38 automated tests, end-to-end run)
- [ ] LAN test: NOT_RUN (needs a second PC and firewall approval)
- [x] Phase 1 report with a go/no-go recommendation

## Phase 2: Gate decisions and lawful groundwork (proposed)

Phase 2 is split by gate G0 (see [feasibility-matrix.md](feasibility-matrix.md)).

**2A. Doable now, needs no game interaction**

1. The user decides on the legal position (EULA versus interoperability law). Record it as a decision.
2. Decide the target platform: Steam (protection conflict) or a DRM-free release (to be verified).
3. Run the prototype over LAN (with approval) and measure RTT, loss and bandwidth.
4. Prototype hardening:
   - fix the real-time timer granularity;
   - add state digests for desync detection;
   - add delta snapshots and a bandwidth budget;
   - add an authenticated transport option.
5. Read-only data-file header statistics and an integrity manifest (E-DATA-1/2).
6. Write the save-protection procedure (hash-manifest tooling) before any launch-based experiment.

**2B. Only if G0 passes:** M1, then M2, then M3, each with explicit go/no-go criteria.

## Later phases (conditional)

- Phase 3: M4 (puppet movement sync), then M5 (combat events)
- Phase 4: M6 (AI) and M7 (Nemesis)
- Phase 5: M8 and M9 (missions, save and recovery)
- Phase 6: M10 (full campaign) and hardening
