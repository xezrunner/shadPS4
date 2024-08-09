// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <unordered_map>
#include <common/types.h>

namespace Input {

struct InputMappings {
    using Scancode = u32;
    using Container = std::unordered_map<Scancode, u32>;

    u32 getMapping(Scancode scancode) const {
        auto it = container.find(scancode);
        return it != container.end() ? it->second : -1;
    }

    void setMapping(Scancode scancode, u32 key) {
        container[scancode] = key;
    }

private:
    Container container;
};

class InputDevices {

public:
    virtual ~InputDevices() = default;
    virtual InputMappings GetMappings() = 0;
};

class SDLKeyboard : public InputDevices {
public:
    virtual ~SDLKeyboard();
    virtual InputMappings GetMappings() override;
};

} // namespace Input