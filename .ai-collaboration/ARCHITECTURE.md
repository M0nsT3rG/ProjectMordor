# Architecture (summary and links)

Full documents:

- [docs/architecture.md](../docs/architecture.md): what is known about the game, and its unknowns
- [docs/protocol.md](../docs/protocol.md): the prototype wire protocol
- [docs/subsystems/](../docs/subsystems/): per-subsystem notes

## Game side: nothing confirmed beyond file headers

- Engine: Firebird/"Kraken", LithTech lineage. **PARTIALLY_CONFIRMED** via third parties and one config key.
- Linked middleware: Wwise and Scaleform. **CONFIRMED** from export names.
- Runtime structure of the game loop, entities, controller, combat, AI, missions and Nemesis: **UNKNOWN or HYPOTHESIS**.

## Prototype (implemented, standalone)

```
src/common      JSON-lines logger, CLI parser
src/network     protocol (22-byte frozen header, 8 message types, strict decode),
                UdpSocket (non-blocking Winsock2), ImpairedSender (seeded loss/latency/jitter/dup)
src/simulation  World + apply_input (shared by host and client), InterpolationBuffer
src/host        Host: fixed tick, input queues, snapshots, timeouts, reconnect grace
src/client      Client: connect/reconnect state machine, prediction + reconciliation, interpolation
tests/          unit (15), integration over loopback UDP (13), network with impairment (10)
```

**Replication model:** host-authoritative and results-only. Clients send inputs; the host sends snapshots. Remote entities are interpolated; the local entity is predicted and reconciled (D-002).

## Intended game integration (design only)

A host bridge and a client bridge would connect the game to this transport. Building them is blocked by gate G0 (see [DECISIONS.md](DECISIONS.md)).
