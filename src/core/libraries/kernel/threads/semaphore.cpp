// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <condition_variable>
#include <mutex>
#include <boost/intrusive/list.hpp>
#include <pthread.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"

namespace Libraries::Kernel {

class Semaphore {
public:
    Semaphore(s32 init_count, s32 max_count, const char* name, bool is_fifo)
        : name{name}, token_count{init_count}, max_count{max_count}, is_fifo{is_fifo} {}

    bool Wait(bool can_block, s32 need_count, u64* timeout) {
        if (need_count < 1 || need_count > max_count) {
            return false;
        }

        std::unique_lock<std::mutex> lock{mutex};
        auto pred = [this, need_count] { return token_count >= need_count; };
        if (!timeout) {
            cond.wait(lock, pred);
            token_count -= need_count;
        } else {
            const auto time_ms = std::chrono::microseconds(*timeout);
            const auto start = std::chrono::high_resolution_clock::now();

            bool result = cond.wait_for(lock, time_ms, pred);

            auto end = std::chrono::high_resolution_clock::now();
            if (result) {
                const auto delta =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                const auto time_remain = time_ms - delta;
                *timeout = time_remain.count();
                token_count -= need_count;
            } else {
                *timeout = 0;
                return false;
            }
        }

        return true;
    }

    bool Signal(s32 signal_count) {
        std::unique_lock<std::mutex> lock{mutex};
        if (token_count + signal_count > max_count) {
            return false;
        }

        token_count += signal_count;
        cond.notify_all();

        return true;
    }

private:
    std::string name;
    s32 token_count;
    std::mutex mutex;
    std::condition_variable cond;
    s32 max_count;
    bool is_fifo;
};

using OrbisKernelSema = Semaphore*;

s32 PS4_SYSV_ABI sceKernelCreateSema(OrbisKernelSema* sem, const char* pName, u32 attr,
                                     s32 initCount, s32 maxCount, const void* pOptParam) {
    if (!pName || attr > 2 || initCount < 0 || maxCount <= 0 || initCount > maxCount) {
        LOG_ERROR(Lib_Kernel, "Semaphore creation parameters are invalid!");
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    *sem = new Semaphore(initCount, maxCount, pName, attr == 1);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceKernelWaitSema(OrbisKernelSema sem, s32 needCount, u64* pTimeout) {
    ASSERT(sem->Wait(true, needCount, pTimeout));
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceKernelSignalSema(OrbisKernelSema sem, s32 signalCount) {
    if (!sem->Signal(signalCount)) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceKernelPollSema(OrbisKernelSema sem, s32 needCount) {
    if (!sem->Wait(false, needCount, nullptr)) {
        return ORBIS_KERNEL_ERROR_EBUSY;
    }
    return ORBIS_OK;
}

void SemaphoreSymbolsRegister(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("188x57JYp0g", "libkernel", 1, "libkernel", 1, 1, sceKernelCreateSema);
    LIB_FUNCTION("Zxa0VhQVTsk", "libkernel", 1, "libkernel", 1, 1, sceKernelWaitSema);
    LIB_FUNCTION("4czppHBiriw", "libkernel", 1, "libkernel", 1, 1, sceKernelSignalSema);
    LIB_FUNCTION("12wOHk8ywb0", "libkernel", 1, "libkernel", 1, 1, sceKernelPollSema);
}

} // namespace Libraries::Kernel
