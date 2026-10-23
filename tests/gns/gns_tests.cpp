// GameNetworkingSockets spike (decision D-012): what the library gives us on loopback, measured in real time.
// Uses only the standalone open-source API (no Steam, no Steamworks, no relay).
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include "test_support.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// GNS reports connection state changes through a plain function pointer, so the spike keeps state in globals.
HSteamListenSocket g_listen = k_HSteamListenSocket_Invalid;
HSteamNetConnection g_server_side = k_HSteamNetConnection_Invalid;

void on_status_changed(SteamNetConnectionStatusChangedCallback_t* info) {
    auto* sockets = SteamNetworkingSockets();
    if (info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid &&
        info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting) {
        if (sockets->AcceptConnection(info->m_hConn) == k_EResultOK) g_server_side = info->m_hConn;
    }
}

void on_debug(ESteamNetworkingSocketsDebugOutputType, const char* msg) { std::fprintf(stderr, "       [gns] %s\n", msg); }

struct GnsLib {
    GnsLib() {
        SteamDatagramErrMsg err{};
        if (!GameNetworkingSockets_Init(nullptr, err)) throw mordor::test::CheckFailure(std::string("GNS init failed: ") + err);
        SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Warning, on_debug);
        SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(on_status_changed);
    }
    ~GnsLib() { GameNetworkingSockets_Kill(); }
};

void set_global_int(ESteamNetworkingConfigValue v, int32 value) {
    CHECK(SteamNetworkingUtils()->SetConfigValue(v, k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Int32, &value));
}
void set_global_float(ESteamNetworkingConfigValue v, float value) {
    CHECK(SteamNetworkingUtils()->SetConfigValue(v, k_ESteamNetworkingConfig_Global, 0, k_ESteamNetworkingConfig_Float, &value));
}

// shortcut: probe a free loopback UDP port, release it and hand it to GNS; another process could grab it in
// between (tiny race), acceptable for a test-only spike.
std::uint16_t free_loopback_port() {
    mordor::net::UdpSocket probe;
    CHECK(probe.open(mordor::net::Endpoint::loopback(0)));
    const auto port = probe.local_endpoint().port;
    probe.close();
    return port;
}

struct Pair {
    ISteamNetworkingSockets* s = SteamNetworkingSockets();
    HSteamNetConnection client = k_HSteamNetConnection_Invalid;

    Pair() {
        g_server_side = k_HSteamNetConnection_Invalid;
        SteamNetworkingIPAddr bind;
        bind.Clear();
        bind.SetIPv4(0x7F000001, free_loopback_port());  // GNS rejects port 0 ("Must specify local port")
        g_listen = s->CreateListenSocketIP(bind, 0, nullptr);
        CHECK(g_listen != k_HSteamListenSocket_Invalid);
        SteamNetworkingIPAddr bound;
        CHECK(s->GetListenSocketAddress(g_listen, &bound));
        SteamNetworkingIPAddr target;
        target.Clear();
        target.SetIPv4(0x7F000001, bound.m_port);
        client = s->ConnectByIPAddress(target, 0, nullptr);
        CHECK(client != k_HSteamNetConnection_Invalid);
        CHECK(pump_until([&] { return state(client) == k_ESteamNetworkingConnectionState_Connected &&
                                      g_server_side != k_HSteamNetConnection_Invalid &&
                                      state(g_server_side) == k_ESteamNetworkingConnectionState_Connected; },
                         std::chrono::seconds(10)));
    }
    ~Pair() {
        s->CloseConnection(client, 0, "test done", false);
        if (g_server_side != k_HSteamNetConnection_Invalid) s->CloseConnection(g_server_side, 0, "test done", false);
        s->CloseListenSocket(g_listen);
        g_listen = k_HSteamListenSocket_Invalid;
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
    void send_counters(HSteamNetConnection c, std::uint32_t first, std::uint32_t n) {
        for (std::uint32_t i = first; i < first + n; ++i)
            CHECK(s->SendMessageToConnection(c, &i, sizeof i, k_nSteamNetworkingSend_Reliable, nullptr) == k_EResultOK);
    }
    // Receives until `n` counters arrived; checks they are exactly first..first+n-1 in order.
    void expect_counters(HSteamNetConnection c, std::uint32_t first, std::uint32_t n, Clock::duration limit) {
        std::uint32_t expect = first;
        CHECK(pump_until([&] {
            SteamNetworkingMessage_t* msgs[64];
            const int got = s->ReceiveMessagesOnConnection(c, msgs, 64);
            for (int i = 0; i < got; ++i) {
                std::uint32_t v = 0;
                const bool ok = msgs[i]->m_cbSize == sizeof v;
                if (ok) std::memcpy(&v, msgs[i]->m_pData, sizeof v);
                msgs[i]->Release();
                CHECK(ok && v == expect);  // in order, no gaps, no duplicates
                ++expect;
            }
            return expect == first + n;
        }, limit));
    }
};

}  // namespace

TEST(gns_loopback_reliable_ordered_both_directions) {
    GnsLib lib;
    Pair p;
    p.send_counters(p.client, 1, 2000);
    p.expect_counters(g_server_side, 1, 2000, std::chrono::seconds(10));
    p.send_counters(g_server_side, 5000, 2000);
    p.expect_counters(p.client, 5000, 2000, std::chrono::seconds(10));
}

// Records what security GNS provides out of the box, without certificates or an identity provider.
TEST(gns_default_connection_security_flags) {
    GnsLib lib;
    Pair p;
    SteamNetConnectionInfo_t info;
    CHECK(p.s->GetConnectionInfo(p.client, &info));
    const bool unencrypted = (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Unencrypted) != 0;
    const bool unauthenticated = (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Unauthenticated) != 0;
    std::printf("       gns flags: unencrypted=%d unauthenticated=%d\n", unencrypted ? 1 : 0, unauthenticated ? 1 : 0);
    CHECK(!unencrypted);  // traffic is encrypted by default
}

// Reliable delivery under GNS's own fake loss / lag / reordering, both directions.
TEST(gns_reliable_exact_under_simulated_loss_and_lag) {
    GnsLib lib;
    set_global_float(k_ESteamNetworkingConfig_FakePacketLoss_Send, 15.0f);
    set_global_int(k_ESteamNetworkingConfig_FakePacketLag_Send, 40);
    set_global_float(k_ESteamNetworkingConfig_FakePacketReorder_Send, 5.0f);
    set_global_int(k_ESteamNetworkingConfig_FakePacketReorder_Time, 20);
    {
        Pair p;
        p.send_counters(p.client, 1, 1000);
        p.expect_counters(g_server_side, 1, 1000, std::chrono::seconds(30));
        p.send_counters(g_server_side, 1, 1000);
        p.expect_counters(p.client, 1, 1000, std::chrono::seconds(30));
        SteamNetConnectionRealTimeStatus_t st{};
        CHECK(p.s->GetConnectionRealTimeStatus(p.client, &st, 0, nullptr) == k_EResultOK);
        std::printf("       gns under 15%% loss/40 ms lag: ping=%d ms quality_local=%.2f\n", st.m_nPing, st.m_flConnectionQualityLocal);
    }
    set_global_float(k_ESteamNetworkingConfig_FakePacketLoss_Send, 0.0f);
    set_global_int(k_ESteamNetworkingConfig_FakePacketLag_Send, 0);
    set_global_float(k_ESteamNetworkingConfig_FakePacketReorder_Send, 0.0f);
}
