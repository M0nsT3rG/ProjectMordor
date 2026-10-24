# Transport interface (design, D-012)

**Status:** **contract v2** after review #4: `PeerId{id, generation}`, lifecycle events, delivery classes, explicit `SendResult`, failure counters. See `src/network/transport.hpp` for the normative comments. The interface and the **UDP backend are implemented** (`src/network/transport.hpp`, `udp_transport.hpp`, `transport_factory.cpp`). Host and client use it, and every existing test passes unchanged (EXP-014). The **GNS adapter is implemented** (`gns_transport.hpp/.cpp`, build with `-DMORDOR_WITH_GNS=ON`; select with `--transport gns` or `MORDOR_TRANSPORT=gns` in tests). See EXP-016 and the section below.

## Secure transport decorator (D-015, EXP-019)

`make_secure_transport(inner, cfg)` returns a `Transport` that carries everything as DTLS 1.2 records over the inner backend's `Unreliable` class.

**Capabilities:**
- `reliable_ordered=false`;
- `reports_disconnects=true`;
- `max_payload` = inner minus 64 bytes;
- `exact_loss_accounting=false`.

**Events:** `Connected` only after DTLS plus the invitation binding; `Disconnected` on rejection, inner loss, or `replaced_by_new_session`.

**Counters:** `secure_stats()` exposes the decorator's own counters (pending opened, refused by cap, failed, authenticated, replaced, dropped non-DTLS).

**Build:** GNS/OpenSSL builds only. In other builds `make_secure_transport` returns `nullptr` with an error, and Host/Client `start()` fails.

## GNS adapter (EXP-016)

- **Runtime.** One process-wide GNS instance, initialised on first use and **never killed** by us (KI-27, EXP-018: rapid Init/Kill cycles exposed a GNS-internal assertion; the first Init waits 50 ms). Callbacks are routed to the owning `GnsTransport` by listen socket or connection handle. Each instance has a poll group.
- **Identity.** Each GNS connection maps to a `PeerId`. A reconnect is a new connection and therefore a new `PeerId` (D-014).
- **Events.** `Connected` on `k_ESteamNetworkingConnectionState_Connected`; `Disconnected` on closed-by-peer or problem-detected-locally.
- **Sends.** `Unreliable` → `UnreliableNoDelay`; `ReliableOrdered` → `Reliable | NoNagle`. `k_EResultLimitExceeded` → `Backpressure`; `NoConnection`/`InvalidState` → `NotConnected`.
- **No silent loss of accepted reliable data.** The adapter tracks accepted-but-unacknowledged reliable data itself, because GNS stops reporting pending bytes after a failure. A failure with such data counts `accepted_then_failed`; `close_peer` and `close()` count `closed_with_unacked_reliable` (per **connection**); `close()` drains for at most 500 ms (KI-26). `discarded_on_close` is per **message**, so GNS leaves it at 0 (review #5 FB-5-01).
- **Counters are wire-level diagnostics, not proof of delivery** (FB-5-01). A wire-acknowledged reliable message may still never be processed by the peer application. Consequential game events will need application ids, acks and resume or replay on top (KI-28).
- **Timeouts.** `TimeoutInitial` 10 s (client), `TimeoutConnected` 3 s.
- **Impairment.** GNS fake loss, lag, jitter, reorder and duplication are **process-global** (`per_instance_impairment=false`, KI-24). There is one effective config (FB-5-03):
  - `set_impairment` returns `false` and counts `impairment_refused` when another instance holds a different config;
  - an instance asking for the same config joins it;
  - the config is reset only when its last holder clears it or closes;
  - `open()` with a conflicting config fails with an error. Mapping: loss → `FakePacketLoss_Send`; latency → `FakePacketLag_Send`; jitter budget split in half between lag jitter and a reorder stage (GNS jitter does not reorder, KI-25); duplication → `FakePacketDup_Send`.
- **Capabilities.** `{reliable_ordered=true, reports_disconnects=true, exact_loss_accounting=false, per_instance_impairment=false}`. Tests branch on capabilities, never on the backend name.

## Goal

Host and client logic (sessions, tokens, snapshots, prediction, events) must not know whether packets travel over:
- the current raw Winsock2 UDP path;
- raw UDP plus the in-house `ReliableChannel`; or
- GameNetworkingSockets.

That lets the same test suites and soak gates run against every transport and compare them.

## Shape (historical sketch; contract v2 in `src/network/transport.hpp` supersedes it)

```cpp
enum class Delivery { UnreliableSequenced, ReliableOrdered };
using PeerId = std::uint32_t;

struct TransportEvent {
    enum Kind { Connected, Disconnected, Message } kind;
    PeerId peer;
    Delivery delivery;                  // for Message
    std::vector<std::uint8_t> bytes;    // for Message
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool listen(const net::Endpoint& bind) = 0;            // host
    virtual std::optional<PeerId> connect(const net::Endpoint&) = 0;  // client
    virtual bool send(PeerId, std::span<const std::uint8_t>, Delivery) = 0;
    virtual void poll(std::uint64_t now_us, std::vector<TransportEvent>& out) = 0;
    virtual void close(PeerId) = 0;
    virtual TransportStats stats(PeerId) const = 0;  // rtt, bytes, loss, encrypted/authenticated flags
};
```

## Mapping

| Concern | Raw UDP (today) | Raw UDP + ReliableChannel | GameNetworkingSockets |
|---|---|---|---|
| Unreliable snapshots and inputs | `sendto` | `sendto` | `k_nSteamNetworkingSend_UnreliableNoDelay` |
| Reliable ordered events | — | `EventMsg`/`EventAck` | `k_nSteamNetworkingSend_Reliable` |
| Connection liveness | our heartbeat + 3 s timeout | same | **GNS owns it** (review #3 answer 2); the adapter reports connect/disconnect events and our timers do not duplicate GNS's |
| Encryption | none | none | built in: on by default (EXP-013) |
| Authentication | none (KI-04) | none | **not by default**: `unauthenticated=1` on a plain IP connection (EXP-013); needs certificates or an identity setup |
| Impairment for tests | `ImpairedSender` | `ImpairedSender` | GNS native `FakePacketLoss/Lag/Reorder/Dup` (review #3 FB-3-06: per-backend fault injection, transport-labelled gates) |
| Port 0 (ephemeral) | supported | supported | **not supported**: GNS requires an explicit listen port (EXP-013) |

## Open questions

Answered by EXP-016: (1) yes, the session layer stays on top; (2) not identical, so gate v4 labels results per transport; (3) see D-013.

The spike and the integration were to answer these:

1. **Session layer on GNS.** Do we keep our protocol's session, token and grace-period logic on top of GNS connections? (Probably yes: GNS connections are transport-level, while our slots and tokens are game-level identity.)
2. **Impairment parity.** Does GNS fake-lag/loss behave closely enough to `ImpairedSender` for the soak gates to stay comparable? If not, the gates need a transport label in a new gate version.
3. **Authentication.** What is the smallest authentication setup that satisfies the WAN prerequisite without any Valve service?
