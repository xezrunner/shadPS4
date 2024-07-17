// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <input/keyboard.h>

SDL_Keycode GetKeyFromString(const std::string& keyString) {
    const std::unordered_map<std::string, SDL_Scancode> keyMap = {
        {"0", SDL_SCANCODE_0},
        {"1", SDL_SCANCODE_1},
        {"2", SDL_SCANCODE_2},
        {"3", SDL_SCANCODE_3},
        {"4", SDL_SCANCODE_4},
        {"5", SDL_SCANCODE_5},
        {"6", SDL_SCANCODE_6},
        {"7", SDL_SCANCODE_7},
        {"8", SDL_SCANCODE_8},
        {"9", SDL_SCANCODE_9},
        {"KP_0", SDL_SCANCODE_KP_0},
        {"KP_1", SDL_SCANCODE_KP_1},
        {"KP_2", SDL_SCANCODE_KP_2},
        {"KP_3", SDL_SCANCODE_KP_3},
        {"KP_4", SDL_SCANCODE_KP_4},
        {"KP_5", SDL_SCANCODE_KP_5},
        {"KP_6", SDL_SCANCODE_KP_6},
        {"KP_7", SDL_SCANCODE_KP_7},
        {"KP_8", SDL_SCANCODE_KP_8},
        {"KP_9", SDL_SCANCODE_KP_9},
        {"A", SDL_SCANCODE_A},
        {"B", SDL_SCANCODE_B},
        {"C", SDL_SCANCODE_C},
        {"D", SDL_SCANCODE_D},
        {"E", SDL_SCANCODE_E},
        {"F", SDL_SCANCODE_F},
        {"G", SDL_SCANCODE_G},
        {"H", SDL_SCANCODE_H},
        {"I", SDL_SCANCODE_I},
        {"J", SDL_SCANCODE_J},
        {"K", SDL_SCANCODE_K},
        {"L", SDL_SCANCODE_L},
        {"M", SDL_SCANCODE_M},
        {"N", SDL_SCANCODE_N},
        {"O", SDL_SCANCODE_O},
        {"P", SDL_SCANCODE_P},
        {"Q", SDL_SCANCODE_Q},
        {"R", SDL_SCANCODE_R},
        {"S", SDL_SCANCODE_S},
        {"T", SDL_SCANCODE_T},
        {"U", SDL_SCANCODE_U},
        {"V", SDL_SCANCODE_V},
        {"W", SDL_SCANCODE_W},
        {"X", SDL_SCANCODE_X},
        {"Y", SDL_SCANCODE_Y},
        {"Z", SDL_SCANCODE_Z},
        {"Left Ctrl", SDL_SCANCODE_LCTRL},
        {"Left Shift", SDL_SCANCODE_LSHIFT},
        {"Right Ctrl", SDL_SCANCODE_RCTRL},
        {"Right Shift", SDL_SCANCODE_RSHIFT},
        {"Up", SDL_SCANCODE_UP},
        {"Down", SDL_SCANCODE_DOWN},
        {"Left", SDL_SCANCODE_LEFT},
        {"Right", SDL_SCANCODE_RIGHT},
    };

    auto it = keyMap.find(keyString);
    if (it != keyMap.end()) {
        return SDL_GetKeyFromScancode(it->second, SDL_KMOD_NONE);
    } else {
        return SDL_GetKeyFromName(keyString.c_str());
    }
}

void Keyboard::initializeSdlKeyMappings(
    const std::unordered_map<std::string, std::string>& mappings,
    std::unordered_map<std::string, SDL_Keycode>& keyMappings) {
    const std::array<std::string, 23> actions = {
        "Up",          "Down",     "Left",       "Right",      "Cross",      "Square",
        "Triangle",    "Circle",   "L1",         "R1",         "L2",         "R2",
        "L3",          "R3",       "Options",    "LStickUp",   "LStickDown", "LStickLeft",
        "LStickRight", "RStickUp", "RStickDown", "RStickLeft", "RStickRight"};

    for (const auto& action : actions) {
        auto it = mappings.find(action);
        if (it != mappings.end()) {
            keyMappings[action] = GetKeyFromString(it->second);
        }
    }
}

void Keyboard::Init() {
    const auto config_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::load(config_dir / "config.toml");
    config_key_map = Config::getKeyMap();
    initializeSdlKeyMappings(config_key_map, sdl_key_map);
}

Keyboard::Keyboard() {}

u32 Keyboard::getButtonState(InputState* state) {
    const Uint8* keystate = SDL_GetKeyboardState(NULL);
    u32 buttons = 0;
    u8 lx = 128, ly = 128, rx = 128, ry = 128;

    if (keystate) {
        for (const auto& key : sdl_key_map) {
            if (keystate[SDL_GetScancodeFromKey(key.second, SDL_KMOD_NONE)]) {
                if (key.first == "Up")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_UP;
                else if (key.first == "Right")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_RIGHT;
                else if (key.first == "Down")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_DOWN;
                else if (key.first == "Left")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_LEFT;
                else if (key.first == "Cross") {
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_CROSS;
                } else if (key.first == "Square")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_SQUARE;
                else if (key.first == "Triangle")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_TRIANGLE;
                else if (key.first == "Circle")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_CIRCLE;
                else if (key.first == "L1")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_L1;
                else if (key.first == "R1")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_R1;
                else if (key.first == "L2")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_L2;
                else if (key.first == "R2")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_R2;
                else if (key.first == "L3")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_L3;
                else if (key.first == "R3")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_R3;
                else if (key.first == "Options")
                    buttons |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_OPTIONS;
                else if (key.first == "LStickUp")
                    state->ly = 0;
                else if (key.first == "LStickDown")
                    state->ly = 255;
                else if (key.first == "LStickLeft")
                    state->lx = 0;
                else if (key.first == "LStickRight")
                    state->lx = 255;
                else if (key.first == "RStickUp")
                    state->ry = 0;
                else if (key.first == "RStickDown")
                    state->ry = 255;
                else if (key.first == "RStickLeft")
                    state->rx = 0;
                else if (key.first == "RStickRight")
                    state->rx = 255;
            }
        }
    }
    return buttons;
}

void Keyboard::getAxis(InputState* state) {
    const Uint8* keystate = SDL_GetKeyboardState(NULL);
    if (keystate) {
        for (const auto& key : sdl_key_map) {
            if (keystate[SDL_GetScancodeFromKey(key.second, SDL_KMOD_NONE)]) {
                if (key.first == "LStickUp")
                    state->ly = 0;
                else if (key.first == "LStickDown")
                    state->ly = 255;
                else if (key.first == "LStickLeft")
                    state->lx = 0;
                else if (key.first == "LStickRight")
                    state->lx = 255;
                else if (key.first == "RStickUp")
                    state->ry = 0;
                else if (key.first == "RStickDown")
                    state->ry = 255;
                else if (key.first == "RStickLeft")
                    state->rx = 0;
                else if (key.first == "RStickRight")
                    state->rx = 255;
            }
        }
    }
}

int Keyboard::GetRumble(u16 smallFreq, u16 bigFreq) {
    return 0;
}