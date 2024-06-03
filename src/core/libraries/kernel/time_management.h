// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Core::Loader {
class SymbolsResolver;
}

namespace Libraries::Kernel {

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

u64 PS4_SYSV_ABI sceKernelGetTscFrequency();
u64 PS4_SYSV_ABI sceKernelGetProcessTime();
u64 PS4_SYSV_ABI sceKernelGetProcessTimeCounter();
u64 PS4_SYSV_ABI sceKernelGetProcessTimeCounterFrequency();
u64 PS4_SYSV_ABI sceKernelReadTsc();
int PS4_SYSV_ABI sceKernelGettimeofday(SceKernelTimeval* tp);
int PS4_SYSV_ABI gettimeofday(struct timeval* tp, struct timezone* tzp);

void timeSymbolsRegister(Core::Loader::SymbolsResolver* sym);

} // namespace Libraries::Kernel
