# Prototype wire protocol (version 2)

Source: [`src/network/protocol.hpp`](../src/network/protocol.hpp). This protocol is **independent of Shadow of War's internal representation**. It moves abstract entities with transforms and has never been connected to the game.

## Transport

- UDP over IPv4. One datagram is one packet. Maximum datagram size is 1,200 bytes.
- Every packet is unreliable. Robustness comes from repetition:
  - inputs are sent redundantly;
  - snapshots supersede each other;
  - Disconnect is sent 3 times unreliably, plus once as `ReliableOrdered` where the backend offers it. It is best effort on both paths, not a delivery guarantee, and the peer's timeout remains the backstop. The unreliable copies were added because the reliable copy alone could wait behind earlier reliable data (EXP-022);
  - connect requests are retried.

## Header (22 bytes, little-endian, frozen across versions)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | magic | ASCII `MRDR` |
| 4 | 2 | version | `kProtocolVersion` = 1 |
| 6 | 1 | type | 1..8, see below |
| 7 | 1 | flags | reserved, 0 |
| 8 | 4 | session | 0 until assigned; then the host's random non-zero session id |
| 12 | 4 | sequence | per-direction packet counter |
| 16 | 4 | ack | latest sequence seen from the peer (informational) |
| 20 | 2 | payload_len | must equal datagram length − 22 |

Rules:

- The header layout and the `ConnectReject` payload never change between versions. A peer with another version can therefore always be told why it was rejected.
- The host answers a `ConnectRequest` with a different version with `ConnectReject{VersionMismatch, host_version}`.
- Every other message with a different version is dropped.

## Messages

| Type | Direction | Payload |
|---|---|---|
| 1 ConnectRequest | C→H | `u64 client_nonce`, `u64 reconnect_token` (0 = new player) |
| 2 ConnectAccept | H→C | `u64 client_nonce` (echo; the client ignores accepts for an older attempt), `u16 player_id`≠0, `u16 entity_id`≠0, `u64 reconnect_token`, `u16 tick_rate_hz` (1..240), `u32 server_tick` |
| 3 ConnectReject | H→C | `u8 reason` (1 VersionMismatch, 2 ServerFull, 3 Malformed), `u16 host_version` |
| 4 Disconnect | both | `u8 reason` (1 ClientQuit, 2 HostShutdown, 3 Timeout, 4 Kicked) |
| 5 Heartbeat | C→H | `u64 send_time_us` |
| 6 HeartbeatAck | H→C | `u64 echo_time_us` (copied from Heartbeat) |
| 7 Input | C→H | `u8 n` (1..8), then n × {`u32 input_seq`, `f32 move_x`, `f32 move_z`}, oldest first; axes finite and within [−1, 1] |
| 8 Snapshot | H→C | `u32 server_tick`, `u32 last_input_seq`, `u8 n` (≤16), then n × {`u16 entity_id`≠0, `u16 owner`, `f32 x,y,z,yaw`, `f32 vel_x, vel_z`}; all floats finite and \|v\| ≤ 1e6 |

The decoder rejects:

- packets that are too short or too long, or have a bad magic or an unknown type;
- packets whose declared length disagrees with the datagram length (including trailing bytes);
- payloads that are truncated or have out-of-range enums;
- NaN or infinite floats, out-of-range axes, zero ids, and counts above the limits.

A 200,000-case fuzz test checks that the decoder never crashes and that every accepted packet re-encodes byte-identically.

### Version 2 (2026-10-24): application-level reliable events (review #9 FB-9-02, KI-28, D-017)

| Type | Direction | Payload |
|---|---|---|
| 9 Event | H→C | `u32 event_id` (≥1, contiguous per player and session), `u16 kind`, `u16 len` (≤256), then `len` bytes |
| 10 EventAck | C→H | `u32 applied_through`: the client **application** has applied every event up to this id |

**Semantics:**
- **Host:**
  - keeps each player's unapplied events, at most 64, beyond which `send_event()` returns false;
  - sends them at once, then resends every unapplied event every 250 ms (unreliable transports) or 1 s (reliable transports), until `EventAck` covers it;
  - replays all unapplied events on a reconnect within the grace period;
  - counts what it drops when a player is removed.
- **Client:**
  - delivers events to the application in id order, each exactly once (later copies are dropped, gaps wait for the resend);
  - acks only after `ack_events_applied()`;
  - resends its latest `EventAck` with every heartbeat;
  - a new host session resets the stream.
- **Not durable:** events do not survive a host restart or a client process restart.

The host also refuses (`event_ack_invalid`) any `EventAck` above the highest event id the transport accepted for that player (review #10 FB-10-01). An event counts as sent only when the transport returned `Sent`. A refused send is retried with back-off (FB-10-02).

### Version 3 (2026-10-24): explicit leave handshake (review #10 FB-10-04, KI-37)

| Type | Direction | Payload |
|---|---|---|
| 11 DisconnectAck | H→C | none: the host received this client's `Disconnect` and released its slot |

**Semantics:**
- **Client.** `leave()` sends `Disconnect` (reliably where the transport offers it, and once unreliably). The client is then in `Leaving`: it resends `Disconnect` every 100 ms until a `DisconnectAck` for its session arrives, or 1 s passes.
  - `stats().leave_confirmed` tells a confirmed leave from an unconfirmed one.
  - The client never reconnects while leaving. If the transport reports the host gone first, the leave ends unconfirmed.
  - `disconnect()` is still the fire-and-forget variant.
- **Host.** On `Disconnect` it sends `DisconnectAck` (twice unreliably, plus reliably where offered) and flushes, **before** it releases the slot and closes the transport peer.
  - A repeated `Disconnect` for the current session from a peer without a slot (our first ack was lost) is acknowledged again (`leave_acks_repeated`). This is a reply to that peer only.
  - On GNS the connection is closed with linger, so the reliable ack is flushed. A repeat cannot arrive after that, and the client learns of the close from the transport.
  - The host keeps the transport peer open for 1.5 s after the leave (KI-37), and closes it earlier if the client's transport goes first. At most 16 peers linger; the oldest is closed early beyond that, unless a player is using that peer again. A successful (re)join removes the peer from the linger list.
  - A host `Disconnect` (e.g. shutdown) received while the client is leaving ends the leave at once, unconfirmed. A `DisconnectAck` for another session is ignored.

## Sessions, identities, sequencing

- **Session.** A random non-zero u32 chosen at host start. After acceptance, packets whose session differs are ignored.
- **Players and entities.** Ids come from monotonically increasing counters and are never reused within a session. Up to 2 players.
- **Packet sequence.** Uses wraparound-safe comparison: `seq_newer(a,b) = a≠b && (u32)(a−b) < 2^31`.
- **Stale data.**
  - The client drops snapshots whose `server_tick` is not newer than the last one applied. That covers reordered and duplicated packets.
  - The host drops input commands whose `input_seq` is not newer than the last one received.

## Simulation, prediction, interpolation

- **Fixed tick.** The host ticks at `tick_rate_hz` (default 30). Each tick applies **exactly one** simulation timestep per entity: one queued input, or zero movement if none is queued (the host never extrapolates).
- **Backlog shedding (review FB-1-01).** If more than 2 inputs would remain queued after a tick's input, the oldest are discarded instead of simulated. They still count as consumed in `last_input_seq`, so the client stops replaying them and reconciliation absorbs the lost movement. Input delay stays bounded and an entity never moves faster than one timestep per tick. Counted in `HostStats::shed_inputs`.
- **Id exhaustion.** Player and entity ids are 16-bit and never reused. Once they are exhausted, the host rejects joins with `ServerFull` instead of wrapping to 0.
- **Grace slots.** A slot in its grace period can only be reclaimed with its reconnect token, never by a token-less request from the same address.
- **Client inputs.** The client produces one input per tick and applies it locally with the same `apply_input()` the host uses. It sends the last ≤8 unacknowledged inputs with each packet, which gives redundancy against loss.
- **Reconciliation.** On each snapshot the client:
  1. drops inputs with `seq ≤ last_input_seq`;
  2. resets its own entity to the authoritative state;
  3. replays the remaining inputs.

  A correction is counted when the result differs from the previous prediction by more than 0.01 units.
- **Remote entities.** Each is buffered by server tick, up to 32 samples. It is rendered at `latest_tick + fraction − 3 ticks`, interpolated linearly, with shortest-path yaw, and clamped at the buffer ends: no extrapolation.

## Liveness, latency, recovery

- **Heartbeat and RTT.** The client sends a Heartbeat every 250 ms. RTT is an EWMA (α = 1/8) of `now − echo_time_us`.
- **Timeout.** Either side declares a timeout after 3 s with no valid packet from the peer. Only messages valid for that direction refresh liveness: Heartbeat, Input and Disconnect at the host; Snapshot, HeartbeatAck and Disconnect at the client. Wrong-direction messages are counted as invalid (review FB-1-02).
- **Timeout notice (KI-12).** A peer can believe it is still connected after the host has timed it out, for example because buffered snapshots refreshed its liveness after a stall. So when a packet carrying the current session arrives from the endpoint of a slot in its grace period, the host replies `Disconnect{Timeout}`.
  - At most once every 250 ms per slot.
  - Never to endpoints without a grace slot, so the host does not reflect traffic to arbitrary sources.
  - The client treats `Disconnect{Timeout}` as "reconnect now with your token", not as a final disconnect.
  - **Stale-notice guard (review FB-2-02).** The client ignores any `Disconnect` whose sequence is not newer than the `ConnectAccept` that established its current connection, so delayed or replayed notices from an earlier grace period have no effect. This guards correctness, not security: an unauthenticated forged packet can still claim a newer sequence (KI-17).
- **Capacity trade-off (review FB-1-07).** The host's 30 s grace period is longer than the client's 15 s reconnect window. A client that gives up therefore leaves its slot reserved for up to about 15 s more. A new join is rejected with `ServerFull` in the meantime.
  - The host keeps a timed-out player's slot and entity for a 30 s grace period. During it, that player still counts toward the 2-player capacity.
  - The client enters `Reconnecting` and sends `ConnectRequest` with its reconnect token every 500 ms for up to 15 s. Then it goes to `Failed`.
- **Reconnect.** A valid token restores the same player id and entity, even from a new address or port.
  - The token is a random 64-bit value. **It is not authentication.** The prototype has no encryption or authentication and must not be exposed to untrusted networks.
- **Graceful leave.** `Disconnect{ClientQuit}` frees the slot immediately. Host shutdown sends `Disconnect{HostShutdown}` to every connected client.

## Impairment simulation

`ImpairedSender` wraps outgoing traffic. It applies seeded loss, fixed latency, uniform jitter (which also reorders packets) and duplication. CLI flags: `--loss`, `--latency-ms`, `--jitter-ms`, `--duplicate`, `--seed`.
