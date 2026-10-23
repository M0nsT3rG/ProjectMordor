# Missions, saves, region transitions, online and networking components

## Missions

- **Known.** Effectively nothing. `SCRI` script assets exist by magic (F-COMM-003). No public mapping of mission state.
- **Co-op approach (design).** Effect capture (F-COOP-004): the host runs mission scripts, records primitive world changes, and the client applies them in order. Mission start and end, and cutscenes, become explicit synchronisation barriers. Both players watch the cutscene; the client waits for the host.
- **Single-player assumptions to expect:**
  - triggers that check "the player" (singular);
  - fail states tied to one character's death;
  - forced teleports;
  - cutscenes that move Talion.
- **Unknown:** whether mission state is serialisable separately from the full save.

## Saves

| Claim | Class | Evidence |
|---|---|---|
| Saves are `<uid>_ShadowOfWar.sav` under `%LOCALAPPDATA%/WB Games/Shadow of War`, magic `SOM3`, AES-CBC encrypted | PARTIALLY_CONFIRMED | F-COMM-010; the directory exists (F-ENV-008); contents not inspected |
| Steam Remote Storage can copy saves into Steam userdata even with Cloud off; the NVIDIA App rewrote `render.cfg` | CONFIRMED (about the source) | F-COOP-013 |

**Rule for every future experiment that launches the game:** first take a hash manifest and backup of `%LOCALAPPDATA%/WB Games/Shadow of War` *and* the Steam userdata folder for app 356190, and verify both after the experiment. The decryption key derivation is deliberately not recorded in this repository.

## Region transitions and loading

- Unknown. These are likely the best barrier points: both PCs load the same region and the host resumes the simulation when both report "ready".

## Online and networking components

| Claim | Class | Evidence |
|---|---|---|
| No `ws2_32.dll` static import; HTTP via wininet/winhttp | CONFIRMED | F-ENV-006 |
| The official backend is `kraken-api.wbagora.com` with a custom binary protocol | HYPOTHESIS | F-COMM-012 |
| The engine source paths keep a client/server split and a "NetMesh" host-vote state machine | CONFIRMED (about the source); runtime use UNKNOWN | F-COOP-011 |
| Official online modes are asynchronous | PARTIALLY_CONFIRMED | F-COMM-013 |

**Boundaries (rules 4, 5, 16):**

- Project Mordor will never talk to, emulate or redirect WB online services.
- Co-op must work fully offline from WB services and must never affect Online Conquest or Vendettas.
- A future co-op mode should refuse to run while the game is in an online mode.
