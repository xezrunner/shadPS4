// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <input/gamepad.h>

Gamepad::Gamepad() {}

void Gamepad::Init() {
    int count;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&count);
    SDL_JoystickID instance_id = joysticks[0];
    gamepad = SDL_OpenGamepad(instance_id);
}

int Gamepad::GetRumble(u16 smallFreq, u16 bigFreq) {
    return gamepad ? SDL_RumbleGamepad(gamepad, smallFreq, bigFreq, -1) : 0;
}

u32 Gamepad::getButtonState(InputState* state) {
    std::unique_lock<std::mutex> lock(m_mutex);
    u32 buttons = 0;
    if (gamepad) {
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_L3)
                       : 0;
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_R3)
                       : 0;
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_OPTIONS)
                       : 0;
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_UP)
                       : 0;
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_RIGHT)
                       : 0;
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_DOWN)
                       : 0;
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_LEFT)
                       : 0;
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_L1)
                       : 0;
        buttons |= SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_L2)
                       : 0;
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_R1)
                       : 0;
        buttons |= SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_R2)
                       : 0;
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_TRIANGLE)
                       : 0;
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_CIRCLE)
                       : 0;
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)
                       ? Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_CROSS
                       : 0;
        buttons |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST)
                       ? (Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_SQUARE)
                       : 0;
    }
    u16 deadzone = 0; // leave as 0 for now.
    s16 leftx = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    s16 lefty = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    s16 rightx = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    s16 righty = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
    s16 triggerL = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    s16 triggerR = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

    state->lx = static_cast<u8>((((std::abs(leftx) > deadzone) ? leftx : 128) + 32768) / 257);
    state->ly = static_cast<u8>((((std::abs(lefty) > deadzone) ? lefty : 128) + 32768) / 257);
    state->rx = static_cast<u8>((((std::abs(rightx) > deadzone) ? rightx : 128) + 32768) / 257);
    state->ry = static_cast<u8>((((std::abs(righty) > deadzone) ? righty : 128) + 32768) / 257);
    state->lt = static_cast<u8>((triggerL + 32768) / 65535) * 255;
    state->rt = static_cast<u8>((triggerR + 32768) / 65535) * 255;
    return buttons;
}

// use this later. maybe.
void Gamepad::getAxis(InputState* state) {
    u16 deadzone = 2500; // Tested with my pdp Xbox One Controller. maybe this should be an option.
    s16 leftx = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    s16 lefty = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    s16 rightx = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    s16 righty = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
    s16 triggerL = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    s16 triggerR = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

    state->lx = static_cast<u8>((((std::abs(leftx) > deadzone) ? leftx : 128) + 32768) / 257);
    state->ly = static_cast<u8>((((std::abs(lefty) > deadzone) ? lefty : 128) + 32768) / 257);
    state->rx = static_cast<u8>((((std::abs(rightx) > deadzone) ? rightx : 128) + 32768) / 257);
    state->ry = static_cast<u8>((((std::abs(righty) > deadzone) ? righty : 128) + 32768) / 257);
    state->lt = static_cast<u8>((triggerL + 32768) / 65535) * 255;
    state->rt = static_cast<u8>((triggerR + 32768) / 65535) * 255;
}