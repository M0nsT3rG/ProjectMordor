#pragma once
// Raw Winsock2 UDP backend behind Transport contract v2. Connectionless: a PeerId is created on the first
// datagram from (or connect() to) an address; close_peer() forgets it, and later traffic from that address
// becomes a new PeerId with generation + 1. ReliableOrdered is not provided (SendResult::Unsupported).
// Codex review of the combined diff (P1): per-source state is bounded. Any datagram from a new address creates a peer
// before the protocol or a DTLS cookie is checked, so the table holds at most max_peers and a peer with no traffic for
// idle_expiry_us is forgotten. When full, the peer with the FEWEST datagrams exchanged (in + out; ties: the oldest)
// is evicted: an established player has thousands, each source of a spoofed flood has one. Least-recently-active
// would evict the player during a flood, since every flood datagram is newer than the player's last one.
// shortcut: a player that joined moments before a flood has few datagrams too and may be evicted; it retries. A
// probation pool for new sources would protect it; upgrade if floods are expected beyond trusted lab networks. Both report Disconnected ("evicted_table_full", "idle_expired"). The generation history is kept only for
// forgotten addresses, with the same bound; losing an entry only restarts the generation count of a long-gone address
// (PeerId ids stay unique).
#include <map>
#include <set>
#include <tuple>
#include <utility>

#include "network/transport.hpp"

namespace mordor::net {

struct UdpPeerLimits {
    std::size_t max_peers = 4096;
    std::uint64_t idle_expiry_us = 120'000'000;  // far above the host's 3 s timeout and reconnect grace
};

class UdpTransport final : public Transport {
public:
    explicit UdpTransport(const ImpairmentConfig& impairment);
    bool open(const Endpoint& bind, bool listen, std::string* error) override;
    void close() override;
    bool is_open() const override { return socket_.is_open(); }
    Endpoint local_endpoint() const override { return socket_.local_endpoint(); }
    TransportCapabilities capabilities() const override;
    std::optional<PeerId> connect(const Endpoint& remote) override;
    SendResult send(PeerId peer, std::span<const std::uint8_t> bytes, Delivery d, std::uint64_t now_us) override;
    void poll(std::uint64_t now_us, std::vector<TransportEvent>& out) override;
    void close_peer(PeerId peer) override;
    void flush(std::uint64_t now_us) override { sender_.flush(now_us); }
    void flush_all() override { sender_.flush_all(); }
    bool set_impairment(const ImpairmentConfig& c) override {
        sender_.set_config(c);
        return true;
    }
    TransportKind kind() const override { return TransportKind::Udp; }
    const TransportStats& stats() const override;
    void set_peer_limits(const UdpPeerLimits& l) { limits_ = l; }
    std::size_t peer_count() const { return by_endpoint_.size(); }
    std::size_t generation_entries() const { return generations_.size(); }

private:
    PeerId peer_for(const Endpoint& e);  // existing or new (queues a Connected event)
    void touch(std::uint32_t id);         // one datagram in or out for a peer, at now_us_
    void forget(std::uint32_t id, std::string_view reason);  // drops a peer, queues Disconnected, records its generation

    UdpSocket socket_;
    ImpairedSender sender_;
    std::map<Endpoint, PeerId> by_endpoint_;
    std::map<std::uint32_t, Endpoint> by_id_;
    std::map<Endpoint, std::uint32_t> generations_;  // next generation for a FORGOTTEN address (bounded)
    struct Activity {
        std::uint64_t last_us = 0, datagrams = 0;
    };
    std::map<std::uint32_t, Activity> activity_;               // peer id -> last datagram time and count (in + out)
    std::set<std::pair<std::uint64_t, std::uint32_t>> by_time_;  // (last_us, id): idle expiry, oldest first
    std::set<std::tuple<std::uint64_t, std::uint64_t, std::uint32_t>> by_count_;  // (datagrams, last_us, id): eviction
    UdpPeerLimits limits_;
    std::uint64_t now_us_ = 0;  // the latest poll()/send() time (connect() has no clock of its own)
    std::vector<TransportEvent> pending_;  // events produced outside poll()
    std::uint32_t next_id_ = 1;
    mutable TransportStats stats_;
};

}  // namespace mordor::net
