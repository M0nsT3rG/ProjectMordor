#include "common/precise_sleep.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION  // Windows 10 1803+; not declared by every SDK header set
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace mordor {

PreciseSleeper::PreciseSleeper()
    : timer_(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)) {}

PreciseSleeper::~PreciseSleeper() {
    if (timer_) CloseHandle(timer_);
}

void PreciseSleeper::sleep_us(std::uint32_t us) {
    if (timer_) {
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(us) * 10;  // relative, 100 ns units
        if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE) && WaitForSingleObject(timer_, INFINITE) == WAIT_OBJECT_0)
            return;
    }
    Sleep(1);
}

}  // namespace mordor
