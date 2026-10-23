#include "network/impairment.hpp"

#include <algorithm>

namespace mordor::net {

void ImpairedSender::transmit(const Endpoint& to, const std::vector<std::uint8_t>& bytes) {
    if (transmit_fn_(to, bytes)) {
        ++stats_.sent;
    } else {
        ++stats_.send_failures;
    }
}

void ImpairedSender::send(const Endpoint& to, std::vector<std::uint8_t> bytes, std::uint64_t now_us) {
    ++stats_.offered;
    if (!cfg_.enabled()) {
        transmit(to, bytes);
        return;
    }
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    if (unit(rng_) < cfg_.loss) {
        ++stats_.dropped;
        return;
    }
    const int copies = unit(rng_) < cfg_.duplicate ? 2 : 1;
    if (copies == 2) ++stats_.duplicated;
    for (int i = 0; i < copies; ++i) {
        std::uint64_t delay = std::uint64_t{cfg_.latency_ms} * 1000;
        if (cfg_.jitter_ms) delay += std::uniform_int_distribution<std::uint64_t>(0, std::uint64_t{cfg_.jitter_ms} * 1000)(rng_);
        queue_.push_back({now_us + delay, to, bytes});
    }
    flush(now_us);
}

void ImpairedSender::flush(std::uint64_t now_us) {
    auto due = std::stable_partition(queue_.begin(), queue_.end(), [&](const Pending& p) { return p.due_us > now_us; });
    for (auto it = due; it != queue_.end(); ++it) transmit(it->to, it->bytes);
    queue_.erase(due, queue_.end());
}

void ImpairedSender::flush_all() {
    for (const auto& p : queue_) transmit(p.to, p.bytes);
    queue_.clear();
}

}  // namespace mordor::net
