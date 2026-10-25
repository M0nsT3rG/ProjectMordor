# Decisions

The authoritative log is [docs/decisions.md](../docs/decisions.md).

## Recorded

| ID | Decision |
|---|---|
| D-001 | Raw Winsock2 UDP, no third-party networking dependency (revisit for NAT traversal and reliable channels) |
| D-002 | Host-authoritative, results-only replication |
| D-003 | No static analysis of the protected on-disk executable |
| D-004 | No RE tooling installed yet |
| D-005 | Third-party RVAs and offsets are hypothesis-only and never compiled in |
| D-006 | Loopback by default; LAN by explicit opt-in |
| D-012 | GameNetworkingSockets behind a `Transport` interface, optional build (user decision) |
| D-014 | On connection-oriented transports the transport owns connection establishment; reconnect opens a new connection (EXP-016) |
| D-009 | GitHub as the shared channel with ChatGPT; work on feature branches; no auto-merge; ChatGPT feedback is advice, not authorisation (2026-10-23) |
| D-007 | Legal position: the owner proceeds with interoperability work and personally accepts the legal risk, without qualified legal advice; minimum necessary, no circumvention; all rules unchanged (2026-10-25). G0 still needs D-008. |

## Pending: owned by the user

| ID | Question | Why it matters |
|---|---|---|
| D-013 | Crypto for authenticated peers. Recommended: DTLS 1.2 (OpenSSL) with pinned RFC 7250 raw public keys; fallback Noise via snow ([evaluation](../docs/d013-evaluation.md)) | Required before any untrusted LAN/WAN use |
| D-008 | Target platform: Steam (protected) or a DRM-free release (GOG, reportedly DRM-free; unverified; both players would need it) | Gate G0 for M1–M10 |
