# Animation and combat

## Known

| Claim | Class | Evidence |
|---|---|---|
| SKEL files are sectioned (bones, model, animation, indices, cloth, footer); bones are keyed by 32-bit name hashes; 364 hashes published | PARTIALLY_CONFIRMED | F-COMM-005 |
| MeshParser converts static `.mesh` (MMSH) to and from FBX | CONFIRMED (about the tool) | F-COMM-006 |
| A `Combat.Action.InventoryItem…` naming scheme exists, and world-drop spawning is claimed | HYPOTHESIS | F-SOWM-011 |
| No public mapping of damage, hit reactions, combo/counter state or executions | CONFIRMED (about the sources) | F-SOWM-013 |

## Unknown

- Whether damage is resolved centrally (one function or event bus) or per weapon or ability.
- How tightly animation drives gameplay, for example root motion and hit frames embedded in animation data. This decides whether a puppet's attacks can be replayed as "play animation X" plus "host applies damage Y".
- How time dilation and slow-motion affect the simulation. Shadow of War uses slow-motion in executions and in the "wraith" mechanics. In co-op, slow-motion on one PC cannot stop time on the other.

## Co-op implications (design)

- Results-only: the host decides hits, damage and deaths. The client plays the matching animations and reactions and never re-simulates outcomes (F-COOP-002).
- Global slow-motion must become local presentation or be disabled. Every slow-motion trigger is a design decision for co-op mode.

## Next experiments

- Read-only header statistics of SKEL and animation files from our own install, to check F-COMM-005. No extraction, no redistribution.
- Everything at runtime is blocked; see the feasibility matrix M5.
