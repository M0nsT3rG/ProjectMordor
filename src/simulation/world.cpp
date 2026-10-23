#include "simulation/world.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mordor::sim {

void apply_input(Entity& e, float mx, float mz, float dt) {
    mx = std::clamp(mx, -1.0f, 1.0f);
    mz = std::clamp(mz, -1.0f, 1.0f);
    const float len = std::sqrt(mx * mx + mz * mz);
    if (len > 1.0f) {
        mx /= len;
        mz /= len;
    }
    e.vx = mx * kMoveSpeed;
    e.vz = mz * kMoveSpeed;
    e.t.x = std::clamp(e.t.x + e.vx * dt, -kWorldHalfExtent, kWorldHalfExtent);
    e.t.z = std::clamp(e.t.z + e.vz * dt, -kWorldHalfExtent, kWorldHalfExtent);
    if (len > 1e-4f) e.t.yaw = std::atan2(mx, mz);
}

Entity& World::spawn(std::uint16_t owner) {
    Entity e;
    e.id = next_id_++;
    e.owner = owner;
    e.t.x = static_cast<float>(e.id - 1) * 2.0f;  // spawn side by side
    entities_.push_back(e);
    return entities_.back();
}

bool World::despawn(std::uint16_t entity_id) {
    return std::erase_if(entities_, [&](const Entity& e) { return e.id == entity_id; }) > 0;
}

Entity* World::find(std::uint16_t entity_id) {
    auto it = std::find_if(entities_.begin(), entities_.end(), [&](const Entity& e) { return e.id == entity_id; });
    return it == entities_.end() ? nullptr : &*it;
}

float lerp_angle(float a, float b, float alpha) {
    constexpr float pi = std::numbers::pi_v<float>;
    float d = std::fmod(b - a + pi, 2 * pi);
    if (d < 0) d += 2 * pi;
    return a + (d - pi) * alpha;
}

bool InterpolationBuffer::push(std::uint32_t tick, const net::Transform& t) {
    if (!samples_.empty() && !net::seq_newer(tick, samples_.back().tick)) return false;
    samples_.push_back({tick, t});
    if (samples_.size() > kCapacity) samples_.pop_front();
    return true;
}

std::optional<net::Transform> InterpolationBuffer::sample(double tick) const {
    // shortcut: interpolation ignores 32-bit tick wraparound (~4.5 years at 30 Hz); fine for a prototype.
    if (samples_.empty()) return std::nullopt;
    if (tick <= samples_.front().tick) return samples_.front().t;
    if (tick >= samples_.back().tick) return samples_.back().t;
    for (std::size_t i = 1; i < samples_.size(); ++i) {
        const auto& b = samples_[i];
        if (b.tick >= tick) {
            const auto& a = samples_[i - 1];
            const float alpha = static_cast<float>((tick - a.tick) / static_cast<double>(b.tick - a.tick));
            net::Transform r;
            r.x = a.t.x + (b.t.x - a.t.x) * alpha;
            r.y = a.t.y + (b.t.y - a.t.y) * alpha;
            r.z = a.t.z + (b.t.z - a.t.z) * alpha;
            r.yaw = lerp_angle(a.t.yaw, b.t.yaw, alpha);
            return r;
        }
    }
    return samples_.back().t;
}

}  // namespace mordor::sim
