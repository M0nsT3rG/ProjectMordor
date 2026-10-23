# Transport interface (design, D-012)

**Status:** the interface and the **UDP backend are implemented** (`src/network/transport.hpp`, `udp_transport.hpp`, `transport_factory.cpp`). Host and client use it, and every existing test passes unchanged (EXP-014). The **GNS adapter is the next milestone.**

## Goal

Host and client logic (sessions, tokens, snapshots, prediction, events) must not know whether packets travel over:
- the current raw Winsock2 UDP path;
- raw UDP plus the in-house `ReliableChannel`; or
- GameNetworkingSockets.

That lets the same test suites and soak gates run against every transport and compare them.

## Shape

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

The spike and the integration will answer these:

1. **Session layer on GNS.** Do we keep our protocol's session, token and grace-period logic on top of GNS connections? (Probably yes: GNS connections are transport-level, while our slots and tokens are game-level identity.)
2. **Impairment parity.** Does GNS fake-lag/loss behave closely enough to `ImpairedSender` for the soak gates to stay comparable? If not, the gates need a transport label in a new gate version.
3. **Authentication.** What is the smallest authentication setup that satisfies the WAN prerequisite without any Valve service?
