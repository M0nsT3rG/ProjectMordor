# Player representation, input, camera, character controller

## Known

| Claim | Class | Evidence |
|---|---|---|
| The game reads mouselook through Win32 Raw Input (`WM_INPUT`) on its window procedure | PARTIALLY_CONFIRMED | F-SOWM-012 (the overlay swallows `WM_INPUT`, implying this) |
| The current player is reachable via engine singleton → `+0x888`; position floats at player `+0x2180..+0x2188` | HYPOTHESIS, low | F-SOWM-010; the author marks the singleton UNVERIFIED and it has no build check |
| The camera transform update can be hooked via a call-site signature; the camera is reached via a GameClient object | HYPOTHESIS | F-COMM-019 |
| No public project maps the character controller | CONFIRMED (about the source) | F-SOWM-013 |

## Unknown, and the central question for co-op

- **Can a second humanoid be driven by an independent input stream?** Talion is presumably controlled by a player controller bound to local devices. A remote player needs either:
  - (a) a second controller instance fed by network input; or
  - (b) an AI-controlled "puppet" body that we steer.

  Neither has been shown for this engine.
- Whether the camera system supports a single viewport only. It probably does, which is fine, since each PC renders its own player.
- Whether gameplay code assumes one player singleton everywhere: UI, quest triggers, Nemesis "player involvement", wraith abilities.

## Co-op implications

- Option (b), the puppet body, is the usual first milestone: the remote player appears as a moved, animated character on the host and vice versa (F-COOP-002). It needs only transform writes and animation triggers, not a second full controller.
- The prototype's prediction and interpolation code maps directly onto this: the local player predicts and the remote puppet interpolates.

## Next experiments

- **None are executable in the Steam build under current rules**, because observing the live player position requires runtime access to a protected process (see the feasibility matrix, M1/M2).
- **Lawful preparatory step:** measure the input-to-network latency budget with the prototype at various tick rates, so we know what the game will need to tolerate.
