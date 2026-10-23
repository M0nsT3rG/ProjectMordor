# Engine, data archives and loading

## Known

| Claim | Class | Evidence |
|---|---|---|
| Engine is Monolith's Firebird (internal "Kraken"), a LithTech derivative | PARTIALLY_CONFIRMED | F-COMM-007, F-COOP-011, F-SOWM-008; our own weak corroboration: `GameClientShell.*` config key (F-ENV-007) |
| Wwise audio and Scaleform UI are statically linked | CONFIRMED | F-ENV-005 |
| Data lives in `*.arch06` archives with magic `LTAR` plus a u32 version; Oodle compression | PARTIALLY_CONFIRMED | F-COMM-002, F-SOWM-016; `oo2core_5_win64.dll` is shipped (F-ENV-001 evidence) |
| Ten asset formats are identified by magic (BNDL, EMBB, LTFX, GADB, LTMI, MMSH, SCRI, SKEL, SKDB) | PARTIALLY_CONFIRMED | F-COMM-003 |
| The game database (GADB) is only partly understood | PARTIALLY_CONFIRMED | F-COMM-004 |
| Resources can be replaced at load time by hooking the file-read routine | HYPOTHESIS (for our build) | F-COMM-009 |
| Monolith is closed, so build 3095001 is likely final | PARTIALLY_CONFIRMED | F-COMM-018 |

## Unknown

- The archive index and file table layout. There is no public extractor beyond a QuickBMS script written for *Shadow of Mordor* (F-SOWM-016).
- What the `SCRI` scripts contain: bytecode, data tables, or both. This matters for mission effect capture.
- Region streaming and loading-screen boundaries: when entities are created and destroyed during region transitions.

## Co-op implications

- Both players must run identical data. A data-hash handshake over `<GAME>/*.arch06` is cheap and fully lawful (it reads our own files only). It could join the protocol's connect phase.
- Region transitions are natural synchronisation barriers: both players load, then the host resumes.

## Next experiments (lawful within the current rules)

- **E-DATA-1.** Write a read-only header reader for `*.arch06` that records magic, version and sizes. Store aggregate statistics only; extract no content. Goal: verify F-COMM-002 on our files. Low risk, because it reads our own install and copies nothing.
- **E-DATA-2.** Build a data-integrity manifest (hash per archive) to use as the co-op "same data" check.
