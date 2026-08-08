// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string_view>

namespace Core::Devtools::InfamousSecondSonDebugMenu {

bool IsAvailable();
bool IsOpen();
void SetOpen(bool open);
void InstallHooks(uintptr_t eboot_base, std::string_view game_serial);
void Draw();

} // namespace Core::Devtools::InfamousSecondSonDebugMenu
