// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <unordered_map>
#include "core/libraries/pad/pad.h"
#include "input_manager.h"

#include "common/config.h"
#include "common/io_file.h"
#include "common/logging/log.h"
#include "common/path_util.h"

class Keyboard : public InputManager {
public:
    Keyboard();
    virtual ~Keyboard() = default;

    virtual u32 getButtonState(InputState* state) override;
    virtual void getAxis(InputState* state) override;
    virtual void Init() override;
    virtual int GetRumble(u16 smallFreq, u16 bigFreq) override;

private:
    std::unordered_map<std::string, std::string> config_key_map;
    void initializeSdlKeyMappings(const std::unordered_map<std::string, std::string>& mappings,
                                  std::unordered_map<std::string, SDL_Keycode>& keyMappings);
    std::unordered_map<std::string, SDL_Keycode> sdl_key_map;
};
