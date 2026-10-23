#pragma once
// Minimal placeholder world: top-down entities moved by normalized 2D input at a fixed tick.
// The same apply_input() runs on the host (authority) and the client (prediction replay).
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "network/protocol.hpp"

namespace mordor::sim {

inline constexpr float kMoveSpeed = 5.0f;  // units per second
inline constexpr float kWorldHalfExtent = 100.0f;

struct Entity {
    std::uint16_t id = 0;
    std::uint16_t owner = 0;
    net::Transform t;
    float vx = 0, vz = 0;
};

void apply_input(Entity& e, float move_x, float move_z, float dt);

class World {
public:
    bool can_spawn() const { return next_id_ != 0; }  // false once the 16-bit id space is exhausted
    Entity& spawn(std::uint16_t owner);               // precondition: can_spawn()
    bool despawn(std::uint16_t entity_id);
    Entity* find(std::uint16_t entity_id);
    const std::vector<Entity>& entities() const { return entities_; }
    std::uint32_t tick() const { return tick_; }
    void advance_tick() { ++tick_; }

private:
    std::vector<Entity> entities_;
    std::uint16_t next_id_ = 1;
    std::uint32_t tick_ = 0;
};

// Buffers authoritative transforms by server tick and samples between them.
class InterpolationBuffer {
public:
    static constexpr std::size_t kCapacity = 32;
    bool push(std::uint32_t tick, const net::Transform& t);  // false if not newer than the latest sample
    std::optional<net::Transform> sample(double tick) const;  // clamps outside the buffered range
    std::size_t size() const { return samples_.size(); }
    void clear() { samples_.clear(); }

private:
    struct Sample {
        std::uint32_t tick;
        net::Transform t;
    };
    std::deque<Sample> samples_;
};

float lerp_angle(float a, float b, float alpha);

}  // namespace mordor::sim
