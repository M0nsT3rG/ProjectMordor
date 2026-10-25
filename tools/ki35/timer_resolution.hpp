// KI-35 probes: 1 ms timer resolution that Windows 11 honours in a windowless child process too (KI-39; the same
// opt-out as tests/test_main.cpp). Without it the probes' sleep_for(1 ms) loops can run at ~15.6 ms (Codex, PR #25).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

inline void request_1ms_timer_resolution() {
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    s.StateMask = 0;  // 0: do NOT ignore our timer resolution request
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof s);
    timeBeginPeriod(1);
}
