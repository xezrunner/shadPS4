// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <thread>
#include "common/native_clock.h"
#include "core/libraries/kernel/time_management.h"
#include "core/libraries/libs.h"

#ifdef _WIN64
#include <windows.h>
#endif

namespace Libraries::Kernel {

static u64 initial_ptc;
static std::unique_ptr<Common::NativeClock> clock;

u64 PS4_SYSV_ABI sceKernelGetTscFrequency() {
    return clock->GetTscFrequency();
}

u64 PS4_SYSV_ABI sceKernelGetProcessTime() {
    return clock->GetProcessTimeUS();
}

u64 PS4_SYSV_ABI sceKernelGetProcessTimeCounter() {
    return clock->GetUptime() - initial_ptc;
}

u64 PS4_SYSV_ABI sceKernelGetProcessTimeCounterFrequency() {
    return clock->GetTscFrequency();
}

u64 PS4_SYSV_ABI sceKernelReadTsc() {
    return clock->GetUptime();
}

int PS4_SYSV_ABI sceKernelUsleep(u32 microseconds) {
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    return 0;
}

int PS4_SYSV_ABI posix_usleep(u32 microseconds) {
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    return 0;
}

u32 PS4_SYSV_ABI sceKernelSleep(u32 seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    return 0;
}

#define FILETIME_1970 116444736000000000ull /* seconds between 1/1/1601 and 1/1/1970 */
#define HECTONANOSEC_PER_SEC 10000000ull

struct SceKernelTimeval {
    time_t tv_sec;
    s64 tv_usec;
};

struct timezone {
    int tz_minuteswest; /* minutes W of Greenwich */
    int tz_dsttime;     /* type of dst correction */
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

int PS4_SYSV_ABI getntptimeofday(struct timespec* tp, struct timezone* z) {
    int res = 0;
    union {
        unsigned long long ns100; /*time since 1 Jan 1601 in 100ns units */
        FILETIME ft;
    } _now;
    TIME_ZONE_INFORMATION TimeZoneInformation;
    DWORD tzi;

    if (z != NULL) {
        if ((tzi = GetTimeZoneInformation(&TimeZoneInformation)) != TIME_ZONE_ID_INVALID) {
            z->tz_minuteswest = TimeZoneInformation.Bias;
            if (tzi == TIME_ZONE_ID_DAYLIGHT)
                z->tz_dsttime = 1;
            else
                z->tz_dsttime = 0;
        } else {
            z->tz_minuteswest = 0;
            z->tz_dsttime = 0;
        }
    }

    if (tp != NULL) {
        typedef void(WINAPI * GetSystemTimeAsFileTime_t)(LPFILETIME);
        static GetSystemTimeAsFileTime_t GetSystemTimeAsFileTime_p /* = 0 */;

        /* Set function pointer during first call */
        GetSystemTimeAsFileTime_t get_time =
            __atomic_load_n(&GetSystemTimeAsFileTime_p, __ATOMIC_RELAXED);
        if (get_time == NULL) {
            /* Use GetSystemTimePreciseAsFileTime() if available (Windows 8 or later) */
            get_time = (GetSystemTimeAsFileTime_t)(intptr_t)GetProcAddress(
                GetModuleHandle("kernel32.dll"),
                "GetSystemTimePreciseAsFileTime"); /* <1us precision on Windows 10 */
            if (get_time == NULL)
                get_time = GetSystemTimeAsFileTime; /* >15ms precision on Windows 10 */
            __atomic_store_n(&GetSystemTimeAsFileTime_p, get_time, __ATOMIC_RELAXED);
        }

        get_time(&_now.ft);                             /* 100 nano-seconds since 1-1-1601 */
        _now.ns100 -= FILETIME_1970;                    /* 100 nano-seconds since 1-1-1970 */
        tp->tv_sec = _now.ns100 / HECTONANOSEC_PER_SEC; /* seconds since 1-1-1970 */
        tp->tv_nsec = (long)(_now.ns100 % HECTONANOSEC_PER_SEC) * 100; /* nanoseconds */
    }
    return res;
}

int PS4_SYSV_ABI gettimeofday(struct timeval* p, struct timezone* z) {
    struct timespec tp;

    if (getntptimeofday(&tp, z))
        return -1;
    p->tv_sec = tp.tv_sec;
    p->tv_usec = (tp.tv_nsec / 1000);
    return 0;
}

void timeSymbolsRegister(Core::Loader::SymbolsResolver* sym) {
    clock = std::make_unique<Common::NativeClock>();
    initial_ptc = clock->GetUptime();
    LIB_FUNCTION("4J2sUJmuHZQ", "libkernel", 1, "libkernel", 1, 1, sceKernelGetProcessTime);
    LIB_FUNCTION("fgxnMeTNUtY", "libkernel", 1, "libkernel", 1, 1, sceKernelGetProcessTimeCounter);
    LIB_FUNCTION("BNowx2l588E", "libkernel", 1, "libkernel", 1, 1,
                 sceKernelGetProcessTimeCounterFrequency);
    LIB_FUNCTION("-2IRUCO--PM", "libkernel", 1, "libkernel", 1, 1, sceKernelReadTsc);
    LIB_FUNCTION("1j3S3n-tTW4", "libkernel", 1, "libkernel", 1, 1, sceKernelGetTscFrequency);
    LIB_FUNCTION("n88vx3C5nW8", "libScePosix", 1, "libkernel", 1, 1, gettimeofday);
    LIB_FUNCTION("n88vx3C5nW8", "libkernel", 1, "libkernel", 1, 1, gettimeofday);
    LIB_FUNCTION("1jfXLRVzisc", "libkernel", 1, "libkernel", 1, 1, sceKernelUsleep);
    LIB_FUNCTION("QcteRwbsnV0", "libScePosix", 1, "libkernel", 1, 1, posix_usleep);
    LIB_FUNCTION("-ZR+hG7aDHw", "libkernel", 1, "libkernel", 1, 1, sceKernelSleep);
    LIB_FUNCTION("0wu33hunNdE", "libScePosix", 1, "libkernel", 1, 1, sceKernelSleep);
}

} // namespace Libraries::Kernel
