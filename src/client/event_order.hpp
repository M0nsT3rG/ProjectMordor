#pragma once
// The application-side contiguity check of the event stream (mordor-client, FB-9-02). The stream is per host session
// and player: a reconnect to the same player keeps it, while a new host session or a new player starts again at id 1
// (as Client does). The check follows that, so a legitimate restart is never counted as an order error (Codex, PR #24).
#include <cstdint>

namespace mordor::client {

struct EventOrderCheck {
    std::uint32_t session = 0;
    std::uint16_t player = 0;
    std::uint32_t last_id = 0;
    std::uint64_t errors = 0;  // gaps or repeats visible to the application

    void apply(std::uint32_t event_session, std::uint16_t event_player, std::uint32_t id) {
        if (event_session != session || event_player != player) {
            session = event_session;
            player = event_player;
            last_id = 0;
        }
        if (id != last_id + 1) ++errors;
        last_id = id;
    }
};

}  // namespace mordor::client
