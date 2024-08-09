// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <SDL3/SDL_keycode.h>
#include "core/libraries/pad/pad.h"
#include "devices.h"

namespace Input {

SDLKeyboard::~SDLKeyboard() {}

InputMappings SDLKeyboard::GetMappings() {
    using Libraries::Pad::OrbisPadButtonDataOffset;
    InputMappings mappings;
    mappings.setMapping(SDLK_UP, OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_UP);
    // TODO the rest
    return mappings;
}
} // namespace Input