#include "network/gns_transport.hpp"

#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <set>
#include <thread>

#include "network/protocol.hpp"

namespace mordor::net {

namespace {

constexpr int kMaxMessagesPerPoll = 1024;
constexpr int kConnectedTimeoutMs = 3000;  // match our protocol's 3 s liveness so the two never disagree
constexpr int kListenPortAttempts = 8;
constexpr int kInitialTimeoutMs = 10000;   // GNS's documented default, set explicitly: the handshake budget (D-014)
constexpr int kCloseDrainMs = 500;         // bounded wait for unacknowledged reliable data in close()

// Process-wide GNS runtime: init/kill refcount and routing of the plain-function status callback to its owner.
struct Runtime {
    int refs = 0;
    bool initialized = false;  // the GNS library lives for the whole process once started (KI-27)
    bool shut_down = false;    // gns_shutdown_for_exit() ran: never initialised again in this process (KI-27/KI-40)
    // Process-global fake impairment (FB-5-03): one effective config. Several instances may hold it only if they
    // asked for the same one; a different request is refused, and the config is reset when the last holder lets go.
    std::set<const GnsTransport*> impairment_holders;
    ImpairmentConfig impairment_active{};
    std::map<HSteamListenSocket, GnsTransport*> listen_owner;
    std::map<HSteamNetConnection, GnsTransport*> conn_owner;
};
Runtime& rt() {
    static Runtime r;
    return r;
}

// KI-42 (review #15 FB-15-02): MORDOR_GNS_TRACE=1 prints every connection lifecycle step to stderr (handles, states,
// reasons; no payloads). Read once; off by default.
bool gns_tracing() {
    static const bool on = [] {
        char* v = nullptr;
        std::size_t n = 0;
        const bool r = _dupenv_s(&v, &n, "MORDOR_GNS_TRACE") == 0 && v && v[0] == '1';
        std::free(v);
        return r;
    }();
    return on;
}
void gns_trace(const char* what, std::uint32_t conn, int a, int b, const char* detail) {
    if (!gns_tracing()) return;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    std::fprintf(stderr, "[gns-trace] %lld %s conn=%u a=%d b=%d %s\n", static_cast<long long>(ms), what, conn, a, b, detail ? detail : "");
}

std::atomic<std::uint64_t> g_unrouted_status{0};  // KI-42: status callbacks no transport claimed (never silent)

void status_cb(SteamNetConnectionStatusChangedCallback_t* info) {
    auto& r = rt();
    const int from = info->m_eOldState, to = info->m_info.m_eState;
    if (auto it = r.conn_owner.find(info->m_hConn); it != r.conn_owner.end()) {
        gns_trace("status", info->m_hConn, from, to, info->m_info.m_szEndDebug);
        it->second->on_status(info);
        return;
    }
    if (info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid)
        if (auto it = r.listen_owner.find(info->m_info.m_hListenSocket); it != r.listen_owner.end()) {
            gns_trace("status_listen", info->m_hConn, from, to, info->m_info.m_szEndDebug);
            it->second->on_status(info);
            return;
        }
    ++g_unrouted_status;
    gns_trace("status_unrouted", info->m_hConn, from, to, info->m_info.m_szEndDebug);
}

std::atomic<std::uint64_t> g_internal_assertions{0};  // KI-27 / FB-6-03: GNS asserts are counted, never ignored

void debug_cb(ESteamNetworkingSocketsDebugOutputType, const char* msg) {
    if (std::strstr(msg, "Assertion Failed")) ++g_internal_assertions;  // GNS service thread: atomic
    std::fprintf(stderr, "[gns] %s\n", msg);
}

void set_global_f(ESteamNetworkingConfigValue v, float x) {
    SteamNetworkingUtils()->SetConfigValue(v, k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Float, &x);
}
void set_global_i(ESteamNetworkingConfigValue v, int32 x) {
    SteamNetworkingUtils()->SetConfigValue(v, k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Int32, &x);
}

// KI-27 / FB-6-03: GNS 1.6.1 marks a closed raw socket as "pending destruction" by clearing its callback and
// deletes it later on its service thread; a debug sanity check in PollRawUDPSockets (socketthread.cpp:2609,
// compiled in release too) asserts if it polls such a socket before that cleanup. Measured (EXP-018, 1200 runs
// per scenario): only a listener closed right after GameNetworkingSockets_Init triggered it (4/1200); with the
// runtime kept alive, or 50 ms after Init, 0/1200. So the library is initialised once per process and never
// killed by us, and the first Init waits for the service thread to settle.
constexpr int kInitSettleMs = 50;

bool acquire(std::string* error) {
    auto& r = rt();
    if (r.shut_down) {  // Kill then Init in one process is what KI-27 warned about: refuse instead
        if (error) *error = "GNS runtime already shut down for this process exit (KI-40)";
        return false;
    }
    if (!r.initialized) {
        SteamDatagramErrMsg err{};
        if (!GameNetworkingSockets_Init(nullptr, err)) {
            if (error) *error = std::string("GameNetworkingSockets_Init failed: ") + err;
            return false;
        }
        SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(status_cb);
        SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Warning, debug_cb);
        // shortcut: a fixed wait, not a readiness signal (GNS exposes none); measured sufficient, EXP-018.
        std::this_thread::sleep_for(std::chrono::milliseconds(kInitSettleMs));
        r.initialized = true;
    }
    ++r.refs;
    return true;
}

void apply_global_impairment(const ImpairmentConfig& c) {
    set_global_f(k_ESteamNetworkingConfig_FakePacketLoss_Send, static_cast<float>(c.loss * 100.0));
    set_global_i(k_ESteamNetworkingConfig_FakePacketLag_Send, static_cast<int32>(c.latency_ms));
    // The profile promises one-way delay <= latency + jitter. GNS applies jitter and then a separate reorder delay,
    // so the jitter budget is split between them (each <= jitter/2) to keep the same worst case. An earlier
    // mapping gave each the full jitter, i.e. up to latency + 2*jitter, worse than the declared profile (EXP-016).
    const float half_jitter = static_cast<float>(c.jitter_ms) / 2.0f;
    set_global_f(k_ESteamNetworkingConfig_FakePacketJitter_Send_Avg, half_jitter / 2.0f);
    set_global_f(k_ESteamNetworkingConfig_FakePacketJitter_Send_Max, half_jitter);
    set_global_f(k_ESteamNetworkingConfig_FakePacketJitter_Send_Pct, c.jitter_ms > 0 ? 100.0f : 0.0f);
    set_global_f(k_ESteamNetworkingConfig_FakePacketDup_Send, static_cast<float>(c.duplicate * 100.0));
    set_global_i(k_ESteamNetworkingConfig_FakePacketDup_TimeMax, static_cast<int32>(c.latency_ms + c.jitter_ms));
    // GNS jitter "by itself will not reorder packets" (header). Our profiles' jitter does reorder, so the same
    // target behaviour also needs GNS's reorder stage (found by the first GNS network run, EXP-016).
    set_global_f(k_ESteamNetworkingConfig_FakePacketReorder_Send, c.jitter_ms > 0 ? 20.0f : 0.0f);
    set_global_i(k_ESteamNetworkingConfig_FakePacketReorder_Time, static_cast<int32>(c.jitter_ms / 2));
}

// Same effect on the wire (GNS ignores the seed).
bool same_impairment(const ImpairmentConfig& a, const ImpairmentConfig& b) {
    return a.loss == b.loss && a.duplicate == b.duplicate && a.latency_ms == b.latency_ms && a.jitter_ms == b.jitter_ms;
}

void drop_impairment_hold(const GnsTransport* t) {
    auto& r = rt();
    if (r.impairment_holders.erase(t) && r.impairment_holders.empty()) {
        apply_global_impairment({});
        r.impairment_active = {};
    }
}

void release() {
    auto& r = rt();
    if (r.refs == 0 || --r.refs > 0) return;
    apply_global_impairment({});  // never leak fake impairment into the next session/test
    r.impairment_holders.clear();
    r.impairment_active = {};
    // No GameNetworkingSockets_Kill (KI-27): the runtime stays up. Lingering connections closed with linger=true
    // keep flushing their reliable data instead of being destroyed (KI-26). The OS reclaims it all at exit.
}

Endpoint to_endpoint(const SteamNetworkingIPAddr& a) { return {a.GetIPv4(), a.m_port}; }

}  // namespace

std::uint64_t gns_internal_assertions() { return g_internal_assertions.load(); }
GnsLifecycle gns_lifecycle() {
    const auto& r = rt();
    return {r.initialized, r.refs, r.conn_owner.size(), r.listen_owner.size()};
}
std::uint64_t gns_unrouted_status_callbacks() { return g_unrouted_status.load(); }

// KI-40 root cause (dump of a hung exit, 2026-10-25): GNS installs its own atexit handler "so that if static
// destruction is triggered without cleaning up the library properly, we won't crash"; it takes the global lock. At
// ExitProcess the service thread is terminated first; if it held that lock at that instant, the handler waits
// forever. Killing the library cleanly before exit stops the service thread while it holds nothing.
bool gns_shutdown_for_exit() {
    auto& r = rt();
    if (!r.initialized || r.shut_down) return true;
    if (r.refs > 0) {  // a transport is still open: refuse rather than kill it under its feet
        gns_trace("shutdown", 0, 0, r.refs, "refused");
        return false;
    }
    gns_trace("shutdown", 0, 1, 0, "killed");  // FB-16-03: makes the early-exit path observable in tests
    GameNetworkingSockets_Kill();
    r.initialized = false;
    r.shut_down = true;
    return true;
}

GnsTransport::GnsTransport(const ImpairmentConfig& impairment) : impairment_(impairment) {}

GnsTransport::~GnsTransport() { close(); }

TransportCapabilities GnsTransport::capabilities() const {
    return {.reliable_ordered = true, .reports_disconnects = true, .proves_return_routability = true, .max_payload = kMaxPacketSize,
            .exact_loss_accounting = false, .per_instance_impairment = false};
}

bool GnsTransport::open(const Endpoint& bind, bool listen, std::string* error) {
    close();
    if (!acquire(error)) return false;
    runtime_acquired_ = true;
    auto* s = SteamNetworkingSockets();
    poll_group_ = s->CreatePollGroup();
    if (listen) {
        // GNS rejects port 0. shortcut: probe a free port, release it and hand it to GNS, retrying if another
        // process grabs it first (tiny race); production hosts should pass an explicit --port.
        for (int attempt = 0; attempt < kListenPortAttempts && listen_ == k_HSteamListenSocket_Invalid; ++attempt) {
            std::uint16_t port = bind.port;
            if (port == 0) {
                UdpSocket probe;
                if (!probe.open(Endpoint{bind.ip, 0}, error)) break;
                port = probe.local_endpoint().port;
                probe.close();
            }
            SteamNetworkingIPAddr addr;
            addr.Clear();
            addr.SetIPv4(bind.ip, port);
            listen_ = s->CreateListenSocketIP(addr, 0, nullptr);
            if (listen_ != k_HSteamListenSocket_Invalid) local_ = {bind.ip, port};
            if (bind.port != 0) break;  // an explicit port is not retried
        }
        if (listen_ == k_HSteamListenSocket_Invalid) {
            if (error && error->empty()) *error = "GNS CreateListenSocketIP failed";
            close();
            return false;
        }
        rt().listen_owner[listen_] = this;
    }
    open_ = true;
    // Process-global setting: opening an unimpaired transport must not wipe impairment another instance set.
    if (impairment_.enabled() && !set_impairment(impairment_)) {
        if (error) *error = "GNS impairment is process-global and another transport holds a different config";
        close();
        return false;
    }
    return true;
}

void GnsTransport::close() {
    if (runtime_acquired_) {
        auto* s = SteamNetworkingSockets();
        // Give reliable data (e.g. a graceful Disconnect) a bounded chance to be acknowledged before closing:
        // GameNetworkingSockets_Kill used to destroy lingering connections (found by the first GNS soak, where
        // clean leaves turned into host-side timeouts, EXP-016). We no longer Kill (KI-27), but a process that
        // exits right after close() still would, so the bounded drain stays.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCloseDrainMs);
        for (const auto& [conn, c] : conns_) s->FlushMessagesOnConnection(conn);
        auto any_unacked = [&] {
            for (const auto& [conn, c] : conns_)
                if (c.reliable_outstanding && has_unacked_reliable(conn)) return true;
            return false;
        };
        while (any_unacked() && std::chrono::steady_clock::now() < deadline) {
            s->RunCallbacks();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (auto& [conn, c] : conns_) {
            // Per CONNECTION, so it goes in closed_with_unacked_reliable; discarded_on_close stays a per-message
            // count, which GNS cannot provide (FB-5-01).
            if (has_unacked_reliable(conn)) ++stats_.closed_with_unacked_reliable;
            s->CloseConnection(conn, 0, "transport closed", true);         // linger: keep sending queued reliable data
            rt().conn_owner.erase(conn);
        }
        if (listen_ != k_HSteamListenSocket_Invalid) {
            rt().listen_owner.erase(listen_);
            s->CloseListenSocket(listen_);
        }
        if (poll_group_ != k_HSteamNetPollGroup_Invalid) s->DestroyPollGroup(poll_group_);
        // Our process-global impairment must not outlive us (found by the first GNS run: a closed host's 100%
        // loss kept blocking the next host in the same process, EXP-016), but it must not end another holder's
        // either (FB-5-03).
        drop_impairment_hold(this);
        // The runtime outlives us (KI-27), so drain the status callbacks our closes just queued; otherwise they
        // pile up until some other instance polls ("Callbacks backing up", EXP-021). Ours are no longer routed
        // (conn_owner entries erased above); other instances' callbacks land in their own pending_ queues.
        s->RunCallbacks();
        release();
    }
    conns_.clear();
    by_peer_id_.clear();
    pending_.clear();
    listen_ = k_HSteamListenSocket_Invalid;
    poll_group_ = k_HSteamNetPollGroup_Invalid;
    runtime_acquired_ = false;
    open_ = false;
}

PeerId GnsTransport::new_peer(const Endpoint& remote) {
    if (!generations_.contains(remote) && generations_.size() >= limits_.max_incoming) generations_.erase(generations_.begin());
    return {next_id_++, generations_[remote]++};
}

std::size_t GnsTransport::incoming_count() const {
    return static_cast<std::size_t>(std::count_if(conns_.begin(), conns_.end(), [](const auto& kv) { return !kv.second.outgoing; }));
}

bool GnsTransport::has_unacked_reliable(std::uint32_t conn) const {
    SteamNetConnectionRealTimeStatus_t st{};
    if (SteamNetworkingSockets()->GetConnectionRealTimeStatus(conn, &st, 0, nullptr) != k_EResultOK) return false;
    return st.m_cbPendingReliable > 0 || st.m_cbSentUnackedReliable > 0;
}

void GnsTransport::forget(std::uint32_t conn, std::string_view reason, bool report) {
    auto it = conns_.find(conn);
    if (it == conns_.end()) return;
    gns_trace("forget", conn, report ? 1 : 0, static_cast<int>(it->second.peer.id), std::string(reason).c_str());
    if (report) pending_.push_back({TransportEvent::Kind::Disconnected, it->second.peer, it->second.remote, Delivery::Unreliable, {}, reason});
    by_peer_id_.erase(it->second.peer.id);
    conns_.erase(it);
    rt().conn_owner.erase(conn);
}

void GnsTransport::on_status(SteamNetConnectionStatusChangedCallback_t* info) {
    auto* s = SteamNetworkingSockets();
    const auto conn = info->m_hConn;
    switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting:
            if (listen_ != k_HSteamListenSocket_Invalid && info->m_info.m_hListenSocket == listen_ && !conns_.contains(conn)) {
                if (incoming_count() >= limits_.max_incoming) {  // bounded before any authentication (Codex review)
                    s->CloseConnection(conn, 0, "connection table full", false);
                    pending_.push_back({TransportEvent::Kind::DroppedInbound, PeerId{}, to_endpoint(info->m_info.m_addrRemote),
                                        Delivery::Unreliable, {}, "connection_table_full"});
                    return;
                }
                if (s->AcceptConnection(conn) != k_EResultOK) {
                    s->CloseConnection(conn, 0, "accept failed", false);
                    return;
                }
                s->SetConnectionPollGroup(conn, poll_group_);
                int32 timeout = kConnectedTimeoutMs;
                SteamNetworkingUtils()->SetConfigValue(k_ESteamNetworkingConfig_TimeoutConnected, k_ESteamNetworkingConfig_Connection,
                                                       conn, k_ESteamNetworkingConfig_Int32, &timeout);
                const auto remote = to_endpoint(info->m_info.m_addrRemote);
                conns_[conn] = Conn{new_peer(remote), remote, false, false};
                conns_[conn].last_msg_us = now_us_;
                by_peer_id_[conns_[conn].peer.id] = conn;
                rt().conn_owner[conn] = this;
            }
            break;
        case k_ESteamNetworkingConnectionState_Connected:
            if (auto it = conns_.find(conn); it != conns_.end() && !it->second.connected_reported) {
                it->second.connected_reported = true;
                pending_.push_back({TransportEvent::Kind::Connected, it->second.peer, it->second.remote, Delivery::Unreliable, {}, "connected"});
            }
            break;
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
            auto it = conns_.find(conn);
            if (it != conns_.end()) {
                // Accepted reliable data that will now never be delivered: surfaced, not silent (review #4).
                // Use our own tracking: GNS no longer reports the pending bytes once the connection has failed.
                if (it->second.reliable_outstanding || has_unacked_reliable(conn)) ++stats_.accepted_then_failed;
                // Messages that arrived before the close are still ours: deliver them, then report the loss.
                // (Before, they were dropped once the connection was forgotten; found by the secure-transport
                // close-drain test on GNS, EXP-021.)
                SteamNetworkingMessage_t* msgs[64];
                for (int got = s->ReceiveMessagesOnConnection(conn, msgs, 64); got > 0; got = s->ReceiveMessagesOnConnection(conn, msgs, 64))
                    for (int i = 0; i < got; ++i) {
                        deliver(it->second, msgs[i], pending_);
                        msgs[i]->Release();
                    }
                const bool report = it->second.connected_reported || it->second.outgoing;
                forget(conn, info->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ? "closed_by_peer"
                                                                                                   : "problem_detected_locally",
                       report);
            }
            s->CloseConnection(conn, 0, nullptr, false);
            break;
        }
        default: break;
    }
}

std::optional<PeerId> GnsTransport::connect(const Endpoint& remote) {
    if (!open_) return std::nullopt;
    for (const auto& [conn, c] : conns_)
        if (c.outgoing && c.remote == remote) return c.peer;  // idempotent while live
    SteamNetworkingIPAddr addr;
    addr.Clear();
    addr.SetIPv4(remote.ip, remote.port);
    SteamNetworkingConfigValue_t opts[2];
    opts[0].SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, kConnectedTimeoutMs);
    opts[1].SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, kInitialTimeoutMs);  // owns handshake time (D-014)
    const auto conn = SteamNetworkingSockets()->ConnectByIPAddress(addr, 2, opts);
    if (conn == k_HSteamNetConnection_Invalid) return std::nullopt;
    SteamNetworkingSockets()->SetConnectionPollGroup(conn, poll_group_);
    conns_[conn] = Conn{new_peer(remote), remote, true, false};
    by_peer_id_[conns_[conn].peer.id] = conn;
    rt().conn_owner[conn] = this;
    gns_trace("connect", conn, static_cast<int>(conns_[conn].peer.id), static_cast<int>(remote.port), "");
    return conns_[conn].peer;
}

SendResult GnsTransport::send(PeerId peer, std::span<const std::uint8_t> bytes, Delivery d, std::uint64_t) {
    auto fail = [&](SendResult r, std::uint64_t& counter) {
        ++counter;
        return r;
    };
    if (!open_) return SendResult::Closed;
    if (bytes.size() > kMaxPacketSize) return fail(SendResult::TooLarge, stats_.too_large);
    const auto it = by_peer_id_.find(peer.id);
    if (it == by_peer_id_.end() || conns_.at(it->second).peer != peer) return fail(SendResult::NotConnected, stats_.not_connected);
    const int flags = d == Delivery::ReliableOrdered ? k_nSteamNetworkingSend_ReliableNoNagle : k_nSteamNetworkingSend_UnreliableNoNagle;
    const EResult r = SteamNetworkingSockets()->SendMessageToConnection(it->second, bytes.data(), static_cast<uint32>(bytes.size()), flags, nullptr);
    switch (r) {
        case k_EResultOK:
            ++stats_.sent;
            if (d == Delivery::ReliableOrdered) conns_.at(it->second).reliable_outstanding = true;
            return SendResult::Sent;
        case k_EResultLimitExceeded: return fail(SendResult::Backpressure, stats_.backpressure);  // send buffer full
        case k_EResultNoConnection:
        case k_EResultInvalidState: return fail(SendResult::NotConnected, stats_.not_connected);
        default: return fail(SendResult::Failed, stats_.send_failed);
    }
}

void GnsTransport::poll(std::uint64_t now_us, std::vector<TransportEvent>& out) {
    if (!runtime_acquired_) return;
    now_us_ = std::max(now_us_, now_us);
    auto* s = SteamNetworkingSockets();
    s->RunCallbacks();  // dispatches status changes for ALL GNS transports in the process into their pending_
    out.insert(out.end(), std::make_move_iterator(pending_.begin()), std::make_move_iterator(pending_.end()));
    pending_.clear();
    if (!open_) return;
    for (auto& [conn, c] : conns_)  // clear the flag once everything reliable has been acknowledged
        if (c.reliable_outstanding && !has_unacked_reliable(conn)) c.reliable_outstanding = false;
    SteamNetworkingMessage_t* msgs[128];
    for (int total = 0; total < kMaxMessagesPerPoll;) {
        const int got = s->ReceiveMessagesOnPollGroup(poll_group_, msgs, 128);
        if (got <= 0) break;
        total += got;
        for (int i = 0; i < got; ++i) {
            auto* m = msgs[i];
            auto it = conns_.find(m->m_conn);
            if (it != conns_.end()) {
                it->second.last_msg_us = now_us_;
                deliver(it->second, m, out);
            }
            m->Release();
        }
    }
    // Codex review: an incoming connection that delivers nothing is closed after idle_expiry_us (GNS keepalives
    // alone would hold it forever). Accepted before the first poll's clock: stamped now, never expired for it.
    std::vector<std::uint32_t> idle;
    for (auto& [conn, c] : conns_) {
        if (c.outgoing) continue;
        if (c.last_msg_us == 0) c.last_msg_us = now_us_;
        if (now_us_ - c.last_msg_us > limits_.idle_expiry_us) idle.push_back(conn);
    }
    for (const auto conn : idle) {
        s->CloseConnection(conn, 0, "idle", false);
        forget(conn, "idle_expired", true);
    }
    out.insert(out.end(), std::make_move_iterator(pending_.begin()), std::make_move_iterator(pending_.end()));
    pending_.clear();
}

void GnsTransport::deliver(Conn& c, SteamNetworkingMessage_t* m, std::vector<TransportEvent>& out) {
    if (!c.connected_reported) {  // contract: Connected precedes the first Message
        c.connected_reported = true;
        out.push_back({TransportEvent::Kind::Connected, c.peer, c.remote, Delivery::Unreliable, {}, "connected"});
    }
    if (static_cast<std::size_t>(m->m_cbSize) > kMaxPacketSize) {
        ++stats_.dropped_inbound_oversized;
        out.push_back({TransportEvent::Kind::DroppedInbound, c.peer, c.remote, Delivery::Unreliable, {}, "oversized_message"});
        return;
    }
    ++stats_.received;
    const auto* p = static_cast<const std::uint8_t*>(m->m_pData);
    out.push_back({TransportEvent::Kind::Message, c.peer, c.remote,
                   (m->m_nFlags & k_nSteamNetworkingSend_Reliable) ? Delivery::ReliableOrdered : Delivery::Unreliable,
                   std::vector<std::uint8_t>(p, p + m->m_cbSize), {}});
}

void GnsTransport::close_peer(PeerId peer) {
    const auto it = by_peer_id_.find(peer.id);
    if (it == by_peer_id_.end() || conns_.at(it->second).peer != peer) return;
    const auto conn = it->second;
    gns_trace("close_peer", conn, static_cast<int>(peer.id), static_cast<int>(peer.generation), "");
    if (has_unacked_reliable(conn)) ++stats_.closed_with_unacked_reliable;  // GNS keeps lingering, but we stop tracking it
    SteamNetworkingSockets()->CloseConnection(conn, 0, "closed by application", true);  // linger: flush reliable data
    forget(conn, "closed_locally", true);
}

void GnsTransport::flush_all() {
    if (!runtime_acquired_) return;
    for (const auto& [conn, c] : conns_) SteamNetworkingSockets()->FlushMessagesOnConnection(conn);
}

// GNS fake impairment is process-global (see header). Mapping from our profile:
// loss -> FakePacketLoss_Send (%), latency -> FakePacketLag_Send, jitter -> FakePacketJitter_Send (exponential,
// avg = jitter/4, max = jitter/2, 100%) plus FakePacketReorder_Send 20% with Reorder_Time = jitter/2 (GNS jitter
// alone never reorders; the two halves keep the profile's worst case latency + jitter), duplicate ->
// FakePacketDup_Send (%) with TimeMax = latency + jitter. Approximate, not identical
// to ImpairedSender's uniform jitter: results are recorded per transport and never compared latency-for-latency.
bool GnsTransport::set_impairment(const ImpairmentConfig& c) {
    if (!runtime_acquired_) {
        impairment_ = c;  // applied (or refused) at open()
        return true;
    }
    auto& r = rt();
    if (!c.enabled()) {
        impairment_ = c;
        drop_impairment_hold(this);
        return true;
    }
    const bool sole_or_none = r.impairment_holders.empty() ||
                              (r.impairment_holders.size() == 1 && r.impairment_holders.contains(this));
    if (!sole_or_none && !same_impairment(c, r.impairment_active)) {
        ++stats_.impairment_refused;  // explicit: another instance's simulation is never silently changed
        return false;
    }
    impairment_ = c;
    r.impairment_holders.insert(this);
    r.impairment_active = c;
    apply_global_impairment(c);
    return true;
}

}  // namespace mordor::net
