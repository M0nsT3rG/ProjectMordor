// GameNetworkingSockets spike (decision D-012): what the library gives us on loopback, measured in real time.
// Uses only the standalone open-source API (no Steam, no Steamworks, no relay).
// Review #3: per-test RAII for all process-global GNS state (FB-3-05), listen-port retries (FB-3-04),
// concurrent / multi-client / mixed-reliability traffic (FB-3-03), explicit security baselines (FB-3-01).
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include <chrono>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "test_support.hpp"

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::uint8_t kReliableTag = 1, kUnreliableTag = 2;

struct Server;
std::map<HSteamListenSocket, Server*>& servers() {  // routes the plain-function GNS callback to its owner
    static std::map<HSteamListenSocket, Server*> m;
    return m;
}

struct Server {
    HSteamListenSocket listen = k_HSteamListenSocket_Invalid;
    std::vector<HSteamNetConnection> accepted;
};

void on_status_changed(SteamNetConnectionStatusChangedCallback_t* info) {
    if (info->m_info.m_hListenSocket == k_HSteamListenSocket_Invalid ||
        info->m_info.m_eState != k_ESteamNetworkingConnectionState_Connecting)
        return;
    auto it = servers().find(info->m_info.m_hListenSocket);
    if (it != servers().end() && SteamNetworkingSockets()->AcceptConnection(info->m_hConn) == k_EResultOK)
        it->second->accepted.push_back(info->m_hConn);
}

void on_debug(ESteamNetworkingSocketsDebugOutputType, const char* msg) { std::fprintf(stderr, "       [gns] %s\n", msg); }

void set_global_int(ESteamNetworkingConfigValue v, int32 value) {
    CHECK(SteamNetworkingUtils()->SetConfigValue(v, k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Int32, &value));
}
void set_global_float(ESteamNetworkingConfigValue v, float value) {
    CHECK(SteamNetworkingUtils()->SetConfigValue(v, k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Float, &value));
}

// Library lifetime plus a guard that restores every process-global setting we touch, even when a CHECK throws.
struct GnsLib {
    GnsLib() {
        SteamDatagramErrMsg err{};
        if (!GameNetworkingSockets_Init(nullptr, err)) throw mordor::test::CheckFailure(std::string("GNS init failed: ") + err);
        SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Warning, on_debug);
        SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(on_status_changed);
    }
    ~GnsLib() {
        float zero_f = 0;
        int32 zero_i = 0;
        auto* u = SteamNetworkingUtils();
        u->SetConfigValue(k_ESteamNetworkingConfig_FakePacketLoss_Send, k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Float, &zero_f);
        u->SetConfigValue(k_ESteamNetworkingConfig_FakePacketReorder_Send, k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Float, &zero_f);
        u->SetConfigValue(k_ESteamNetworkingConfig_FakePacketLag_Send, k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Int32, &zero_i);
        u->SetGlobalCallback_SteamNetConnectionStatusChanged(nullptr);
        servers().clear();
        GameNetworkingSockets_Kill();
    }
};

struct Net {
    ISteamNetworkingSockets* s = SteamNetworkingSockets();
    Server server;
    std::vector<HSteamNetConnection> clients;

    // FB-3-04: GNS rejects port 0, so probe a free port; if another process grabs it before GNS binds,
    // retry with a fresh port (bounded) instead of failing the test.
    Net() {
        for (int attempt = 0; attempt < 8 && server.listen == k_HSteamListenSocket_Invalid; ++attempt) {
            mordor::net::UdpSocket probe;
            CHECK(probe.open(mordor::net::Endpoint::loopback(0)));
            const auto port = probe.local_endpoint().port;
            probe.close();
            SteamNetworkingIPAddr bind;
            bind.Clear();
            bind.SetIPv4(0x7F000001, port);
            server.listen = s->CreateListenSocketIP(bind, 0, nullptr);
        }
        CHECK(server.listen != k_HSteamListenSocket_Invalid);
        servers()[server.listen] = &server;
    }
    ~Net() {
        for (auto c : clients) s->CloseConnection(c, 0, "test done", false);
        for (auto c : server.accepted) s->CloseConnection(c, 0, "test done", false);
        servers().erase(server.listen);
        s->CloseListenSocket(server.listen);
    }
    HSteamNetConnection connect() {
        SteamNetworkingIPAddr bound;
        CHECK(s->GetListenSocketAddress(server.listen, &bound));
        SteamNetworkingIPAddr target;
        target.Clear();
        target.SetIPv4(0x7F000001, bound.m_port);
        const auto c = s->ConnectByIPAddress(target, 0, nullptr);
        CHECK(c != k_HSteamNetConnection_Invalid);
        clients.push_back(c);
        const auto want = clients.size();
        CHECK(pump_until([&] {
            if (server.accepted.size() < want) return false;
            for (auto x : clients)
                if (state(x) != k_ESteamNetworkingConnectionState_Connected) return false;
            for (auto x : server.accepted)
                if (state(x) != k_ESteamNetworkingConnectionState_Connected) return false;
            return true;
        }, std::chrono::seconds(10)));
        return c;
    }
    ESteamNetworkingConnectionState state(HSteamNetConnection c) {
        SteamNetConnectionInfo_t info;
        return s->GetConnectionInfo(c, &info) ? info.m_eState : k_ESteamNetworkingConnectionState_None;
    }
    template <class Pred>
    bool pump_until(Pred done, Clock::duration limit) {
        const auto end = Clock::now() + limit;
        while (Clock::now() < end) {
            s->RunCallbacks();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
    EResult send(HSteamNetConnection c, std::uint8_t tag, std::uint32_t seq) {
        std::uint8_t b[5] = {tag};
        std::memcpy(b + 1, &seq, 4);
        const int flags = tag == kReliableTag ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoNagle;
        return s->SendMessageToConnection(c, b, sizeof b, flags, nullptr);
    }
};

// Collects messages per connection and checks the reliable stream is exactly 1..n in order and the unreliable
// stream never repeats (it may lose messages).
struct Receiver {
    std::uint32_t next_reliable = 1;
    std::set<std::uint32_t> unreliable_seen;
    std::uint64_t duplicates = 0, malformed = 0, out_of_order = 0;
    void drain(ISteamNetworkingSockets* s, HSteamNetConnection c) {
        SteamNetworkingMessage_t* msgs[128];
        for (int got = s->ReceiveMessagesOnConnection(c, msgs, 128); got > 0; got = s->ReceiveMessagesOnConnection(c, msgs, 128)) {
            for (int i = 0; i < got; ++i) {
                std::uint32_t seq = 0;
                const auto* p = static_cast<const std::uint8_t*>(msgs[i]->m_pData);
                if (msgs[i]->m_cbSize != 5) {
                    ++malformed;
                } else {
                    std::memcpy(&seq, p + 1, 4);
                    if (p[0] == kReliableTag) {
                        if (seq == next_reliable) ++next_reliable;
                        else ++out_of_order;
                    } else if (!unreliable_seen.insert(seq).second) {
                        ++duplicates;
                    }
                }
                msgs[i]->Release();
            }
        }
    }
    void check_clean() const {
        CHECK(duplicates == 0);
        CHECK(malformed == 0);
        CHECK(out_of_order == 0);
    }
};

}  // namespace

// FB-3-03: traffic flows in both directions at the same time, not one direction after the other.
TEST(gns_concurrent_bidirectional_reliable_ordered) {
    GnsLib lib;
    Net n;
    const auto c = n.connect();
    const auto srv = n.server.accepted[0];
    constexpr std::uint32_t kN = 3000;
    Receiver at_server, at_client;
    std::uint32_t sent = 0;
    CHECK(n.pump_until([&] {
        for (int burst = 0; burst < 50 && sent < kN; ++burst) {
            ++sent;
            CHECK(n.send(c, kReliableTag, sent) == k_EResultOK);
            CHECK(n.send(srv, kReliableTag, sent) == k_EResultOK);
        }
        at_server.drain(n.s, srv);
        at_client.drain(n.s, c);
        return at_server.next_reliable == kN + 1 && at_client.next_reliable == kN + 1;
    }, std::chrono::seconds(20)));
    at_server.check_clean();
    at_client.check_clean();
}

// FB-3-03: two simultaneous clients on one listen socket; each stream stays exact and ordered.
TEST(gns_two_simultaneous_clients) {
    GnsLib lib;
    Net n;
    const auto c1 = n.connect();
    const auto c2 = n.connect();
    CHECK(n.server.accepted.size() == 2);
    // Map each accepted connection to its client by remote port.
    std::map<HSteamNetConnection, Receiver> rx;
    constexpr std::uint32_t kN = 2000;
    std::uint32_t sent = 0;
    CHECK(n.pump_until([&] {
        for (int burst = 0; burst < 50 && sent < kN; ++burst) {
            ++sent;
            CHECK(n.send(c1, kReliableTag, sent) == k_EResultOK);
            CHECK(n.send(c2, kReliableTag, sent) == k_EResultOK);
        }
        for (auto a : n.server.accepted) rx[a].drain(n.s, a);
        return rx.size() == 2 && rx.begin()->second.next_reliable == kN + 1 && rx.rbegin()->second.next_reliable == kN + 1;
    }, std::chrono::seconds(20)));
    for (auto& [conn, r] : rx) r.check_clean();
}

// FB-3-03: reliable "events" interleaved with unreliable "snapshots" on one connection, under loss and lag.
// Reliable must be exact and ordered; unreliable may drop but never duplicates.
TEST(gns_interleaved_reliable_and_unreliable_under_loss) {
    GnsLib lib;
    set_global_float(k_ESteamNetworkingConfig_FakePacketLoss_Send, 10.0f);
    set_global_int(k_ESteamNetworkingConfig_FakePacketLag_Send, 30);
    Net n;
    const auto c = n.connect();
    const auto srv = n.server.accepted[0];
    constexpr std::uint32_t kN = 1000;
    Receiver rx;
    std::uint32_t sent = 0;
    CHECK(n.pump_until([&] {
        for (int burst = 0; burst < 20 && sent < kN; ++burst) {
            ++sent;
            CHECK(n.send(c, kReliableTag, sent) == k_EResultOK);
            CHECK(n.send(c, kUnreliableTag, sent) == k_EResultOK);
        }
        rx.drain(n.s, srv);
        return rx.next_reliable == kN + 1;
    }, std::chrono::seconds(30)));
    rx.check_clean();
    CHECK(rx.unreliable_seen.size() <= kN);
    std::printf("       interleaved: reliable %u/%u exact; unreliable delivered %zu/%u (10%% fake loss)\n", kN, kN,
                rx.unreliable_seen.size(), kN);
}

// FB-3-03: send-queue pressure. Every message the API accepted must arrive exactly once, in order.
TEST(gns_send_queue_pressure) {
    GnsLib lib;
    Net n;
    const auto c = n.connect();
    const auto srv = n.server.accepted[0];
    std::uint32_t accepted = 0, refused = 0;
    for (std::uint32_t i = 1; i <= 200'000; ++i) {
        std::uint8_t b[5] = {kReliableTag};
        const std::uint32_t seq = accepted + 1;
        std::memcpy(b + 1, &seq, 4);
        if (n.s->SendMessageToConnection(c, b, sizeof b, k_nSteamNetworkingSend_Reliable, nullptr) == k_EResultOK) ++accepted;
        else ++refused;
    }
    Receiver rx;
    CHECK(n.pump_until([&] {
        rx.drain(n.s, srv);
        return rx.next_reliable == accepted + 1;
    }, std::chrono::seconds(60)));
    rx.check_clean();
    std::printf("       queue pressure: accepted %u, refused %u (backpressure), all accepted delivered in order\n", accepted, refused);
}

// FB-3-01 negative baseline: plain IP connections are encrypted but NOT authenticated.
TEST(gns_default_connection_security_flags) {
    GnsLib lib;
    Net n;
    const auto c = n.connect();
    for (auto conn : {c, n.server.accepted[0]}) {
        SteamNetConnectionInfo_t info;
        CHECK(n.s->GetConnectionInfo(conn, &info));
        CHECK((info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Unencrypted) == 0);   // encrypted
        CHECK((info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Unauthenticated) != 0);  // peer NOT authenticated
    }
}

// FB-3-01 negative baseline: the open-source build accepts unauthenticated peers on ANY IP by default
// (source: csteamnetworkingsockets.cpp, STEAMNETWORKINGSOCKETS_OPENSOURCE defaults). Pin it so a library
// update that changes this is noticed.
TEST(gns_opensource_build_accepts_unauthenticated_peers_by_default) {
    GnsLib lib;
    for (auto v : {k_ESteamNetworkingConfig_IP_AllowWithoutAuth, k_ESteamNetworkingConfig_IPLocalHost_AllowWithoutAuth}) {
        int32 value = -1;
        size_t size = sizeof value;
        ESteamNetworkingConfigDataType type{};
        const auto r = SteamNetworkingUtils()->GetConfigValue(v, k_ESteamNetworkingConfig_Global, 0, &type, &value, &size);
        CHECK(r == k_ESteamNetworkingGetConfigValue_OK || r == k_ESteamNetworkingGetConfigValue_OKInherited);
        CHECK(value == 2);  // 2 = don't attempt authentication, don't complain if the peer is unauthenticated
    }
}

// Reliable delivery under GNS's own fake loss / lag / reordering, both directions.
TEST(gns_reliable_exact_under_simulated_loss_and_lag) {
    GnsLib lib;
    set_global_float(k_ESteamNetworkingConfig_FakePacketLoss_Send, 15.0f);
    set_global_int(k_ESteamNetworkingConfig_FakePacketLag_Send, 40);
    set_global_float(k_ESteamNetworkingConfig_FakePacketReorder_Send, 5.0f);
    set_global_int(k_ESteamNetworkingConfig_FakePacketReorder_Time, 20);
    Net n;
    const auto c = n.connect();
    const auto srv = n.server.accepted[0];
    Receiver at_server, at_client;
    for (std::uint32_t i = 1; i <= 1000; ++i) {
        CHECK(n.send(c, kReliableTag, i) == k_EResultOK);
        CHECK(n.send(srv, kReliableTag, i) == k_EResultOK);
    }
    CHECK(n.pump_until([&] {
        at_server.drain(n.s, srv);
        at_client.drain(n.s, c);
        return at_server.next_reliable == 1001 && at_client.next_reliable == 1001;
    }, std::chrono::seconds(30)));
    at_server.check_clean();
    at_client.check_clean();
    SteamNetConnectionRealTimeStatus_t st{};
    CHECK(n.s->GetConnectionRealTimeStatus(c, &st, 0, nullptr) == k_EResultOK);
    std::printf("       gns under 15%% loss/40 ms lag: ping=%d ms\n", st.m_nPing);
}
