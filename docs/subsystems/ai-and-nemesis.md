# AI actors and the Nemesis system

## Known

| Claim | Class | Evidence |
|---|---|---|
| The Nemesis system is called "Faction" internally (`Faction.Debug.*`, `FactionMember.Debug.Resurrection`) | PARTIALLY_CONFIRMED | F-COMM-011 |
| Nemesis logic runs on one game thread; every event is dispatched through a central handler (RVA `40C304` claimed); outcomes are drawn from one global 64-bit LCG | HYPOTHESIS | F-COOP-012; the source claims our hash, but the method was runtime probing that we cannot reproduce and no code is public |
| Nemesis state persists in the encrypted save | PARTIALLY_CONFIRMED | F-COMM-010 |
| WB holds US patent 10,926,179 B2 on the Nemesis design, active to 2036 | CONFIRMED (about the patent record) | F-COMM-016 |
| Official online Nemesis features (Online Conquest, Vendettas) are asynchronous, not real-time | PARTIALLY_CONFIRMED | F-COMM-013 |

## Unknown

- How AI actors are registered, owned and updated. Is there an actor list? Are there stable ids that survive save and load?
- Whether Nemesis events are deterministic given the LCG seed and the same inputs. If yes, a "same seed, same events" scheme could work. If not, the host must send results.
- How promotions, betrayals and deaths are written to the save. These are probably mutations of a large orc-roster structure.

## Co-op design (from the methodology, F-COOP-003/004/005)

- **One authority per actor.** The host owns every AI actor by default. Ownership can migrate to the nearer player for responsiveness, using an epoch counter so stale updates are rejected.
- **Nemesis is host-only.** Only the host's game runs Nemesis logic and rolls the LCG. Clients receive an ordered list of effects (orc promoted, orc killed, trait added, ...) and display them.
- **Canonical save.** The host's save is the single truth. Clients never write Nemesis state into their own saves; protecting their single-player save is mandatory (F-COOP-013).

## Next experiments

- None at runtime under current rules (M6 and M7 in the feasibility matrix).
- **Save inspection.** Reading our own save file would require decrypting it. Decryption of one's own save is a separate legal question from anti-tamper. **Not done in Phase 1.** It needs a user decision, and it must run on a copy with a hash manifest of the original.
