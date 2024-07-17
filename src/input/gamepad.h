// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>

#include "common/config.h"
#include "common/io_file.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/libraries/pad/pad.h"
#include "input_manager.h"

class Gamepad : public InputManager {
public:
    Gamepad();
    virtual ~Gamepad() = default;

    virtual void Init() override;
    virtual u32 getButtonState(InputState* state) override;
    virtual void getAxis(InputState* state) override;
    virtual int GetRumble(u16 smallFreq, u16 bigFreq) override;

private:
    SDL_Gamepad* gamepad;
    std::mutex m_mutex;
};
