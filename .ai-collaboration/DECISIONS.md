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
| D-009 | GitHub as the shared channel with ChatGPT; work on feature branches; no auto-merge; ChatGPT feedback is advice, not authorisation (2026-10-23) |

## Pending: owned by the user

| ID | Question | Why it matters |
|---|---|---|
| D-007 | Legal position: the EULA prohibits reverse engineering; EU Directive 2009/24/EC Articles 6 and 8 may permit interoperability work. Needs qualified legal advice. | Gate G0 for M1–M10 |
| D-008 | Target platform: Steam (protected) or a DRM-free release (GOG, reportedly DRM-free; unverified; both players would need it) | Gate G0 for M1–M10 |
