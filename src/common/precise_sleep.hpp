#pragma once
// Short sleeps for the executables' main loops (KI-02). std::this_thread::sleep_for(1ms) can sleep
// ~15.6 ms on Windows' default timer resolution, adding ~50 ms to measured RTT. A high-resolution
// waitable timer is per-thread and does not change any system-wide setting. Falls back to Sleep(1).
#include <cstdint>

namespace mordor {

class PreciseSleeper {
public:
    PreciseSleeper();
    ~PreciseSleeper();
    PreciseSleeper(const PreciseSleeper&) = delete;
    PreciseSleeper& operator=(const PreciseSleeper&) = delete;
    void sleep_us(std::uint32_t us);
    bool high_resolution() const { return timer_ != nullptr; }

private:
    void* timer_ = nullptr;  // HANDLE
};

}  // namespace mordor
