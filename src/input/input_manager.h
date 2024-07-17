#pragma once

#include <memory>
#include <vector>
#include <SDL3/SDL.h>
#include "common/types.h"

struct InputState {
    u8 lx = 128, ly = 128, rx = 128, ry = 128, lt = 0, rt = 0;
};

class InputManager {
public:
    virtual ~InputManager() = default;

    virtual u32 getButtonState(InputState* state) = 0;
    virtual void getAxis(InputState* state) = 0;
    virtual void Init() = 0;
    virtual int GetRumble(u16 smallFreq, u16 bigFreq) = 0;
};