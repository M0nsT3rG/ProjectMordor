#pragma once
// GameNetworkingSockets backend behind Transport contract v2 (D-012, review #4 next steps).
// Standalone open-source API only: no Steam, no relay, no AppID.
//
// Verified constraints (GNS 1.6.1 headers, steamnetworkingtypes.h):
//  - Fake loss/lag/jitter/reorder/dup are GLOBAL (process-wide), not per connection. set_impairment() therefore
//    applies to every GNS transport in the process; capabilities().per_instance_impairment == false.
//    Several instances may hold the same config; a DIFFERENT one is refused (returns false) while another
//    instance holds one, and the config is reset only when the last holder clears it or closes (FB-5-03).
//  - Listen sockets need an explicit port (port 0 is rejected); open() probes a free port with retries.
//  - Plain-IP connections are encrypted but NOT authenticated (docs/authentication-design.md); lab use only.
#include <map>

#include "network/transport.hpp"

struct SteamNetConnectionStatusChangedCallback_t;
struct SteamNetworkingMessage_t;

namespace mordor::net {

// Number of "Assertion Failed" messages GNS reported through its debug output in this process (KI-27).
std::uint64_t gns_internal_assertions();
// KI-40 lifecycle diagnostics: what this process still holds in the GNS runtime (0/0/0 once every transport closed).
struct GnsLifecycle {
    bool initialized = false;
    int refs = 0;
    std::size_t connections = 0, listen_sockets = 0;
};
GnsLifecycle gns_lifecycle();
// KI-42: GNS connection status callbacks that no transport in this process claimed (they used to vanish silently).
std::uint64_t gns_unrouted_status_callbacks();
// KI-40: call once at the very end of main, after every GNS transport is closed. Stops the GNS service thread so
// that GNS's own atexit handler cannot deadlock on a lock the terminated thread held. Afterwards no GNS transport
// can be opened in this process (no re-initialisation, KI-27). Returns false (and does nothing) while any is open.
bool gns_shutdown_for_exit();

// Codex review of the combined diff (P2): incoming connections are accepted before any DTLS or session handshake, so
// they are bounded like the UDP peer table: at most max_incoming at once (more are refused), and an incoming connection
// that delivers no message for idle_expiry_us is closed ("idle_expired"); GNS's own keepalives no longer hold it open.
// The per-address generation history has the same bound.
// shortcut: a peer that keeps a connection busy with junk messages still holds a slot until the application closes
// it (the session layer never adopts it); a pre-authentication deadline in SecureTransport would close it sooner.
// Upgrade before any untrusted network.
struct GnsPeerLimits {
    std::size_t max_incoming = 1024;
    std::uint64_t idle_expiry_us = 120'000'000;
};

class GnsTransport final : public Transport {
public:
    explicit GnsTransport(const ImpairmentConfig& impairment);
    ~GnsTransport() override;
    GnsTransport(const GnsTransport&) = delete;
    GnsTransport& operator=(const GnsTransport&) = delete;

    bool open(const Endpoint& bind, bool listen, std::string* error) override;
    void close() override;
    bool is_open() const override { return open_; }
    Endpoint local_endpoint() const override { return local_; }
    TransportCapabilities capabilities() const override;
    std::optional<PeerId> connect(const Endpoint& remote) override;
    SendResult send(PeerId peer, std::span<const std::uint8_t> bytes, Delivery d, std::uint64_t now_us) override;
    void poll(std::uint64_t now_us, std::vector<TransportEvent>& out) override;
    void flush(std::uint64_t) override {}  // GNS sends from its own service thread; NoNagle flags avoid batching delay
    void close_peer(PeerId peer) override;
    void flush_all() override;
    bool set_impairment(const ImpairmentConfig& c) override;
    TransportKind kind() const override { return TransportKind::Gns; }
    const TransportStats& stats() const override { return stats_; }
    void set_peer_limits(const GnsPeerLimits& l) { limits_ = l; }
    std::size_t incoming_count() const;

    // Internal: called from the process-wide status callback.
    void on_status(SteamNetConnectionStatusChangedCallback_t* info);

private:
    struct Conn {
        PeerId peer;
        Endpoint remote;
        bool outgoing = false;
        bool connected_reported = false;
        // Set when reliable data is accepted; cleared once GNS reports nothing pending or unacknowledged.
        // GNS's own counters are no longer reliable after the connection fails, so we keep this ourselves.
        bool reliable_outstanding = false;
        std::uint64_t last_msg_us = 0;  // incoming only: accept time, then the latest message (idle expiry)
    };
    PeerId new_peer(const Endpoint& remote);
    void deliver(Conn& c, SteamNetworkingMessage_t* m, std::vector<TransportEvent>& out);
    void forget(std::uint32_t conn, std::string_view reason, bool report);
    bool has_unacked_reliable(std::uint32_t conn) const;

    ImpairmentConfig impairment_;
    bool open_ = false;
    bool runtime_acquired_ = false;
    Endpoint local_{};
    std::uint32_t listen_ = 0;      // HSteamListenSocket
    std::uint32_t poll_group_ = 0;  // HSteamNetPollGroup
    std::map<std::uint32_t, Conn> conns_;  // HSteamNetConnection -> state
    std::map<std::uint32_t, std::uint32_t> by_peer_id_;  // PeerId.id -> HSteamNetConnection
    std::map<Endpoint, std::uint32_t> generations_;
    std::vector<TransportEvent> pending_;
    std::uint32_t next_id_ = 1;
    TransportStats stats_;
    GnsPeerLimits limits_;
    std::uint64_t now_us_ = 0;  // the latest poll() time
};

}  // namespace mordor::net
