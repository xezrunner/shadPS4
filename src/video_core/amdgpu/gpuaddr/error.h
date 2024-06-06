// SPDX-FileCopyrightText: Copyright 2024 freegnm Project
// SPDX-License-Identifier: MIT

#pragma once

#include <string_view>

enum GpaError {
    GPA_ERR_OK = 0,
    GPA_ERR_INVALID_ARGS,
    GPA_ERR_OVERFLOW,
    GPA_ERR_TILING_ERROR,
    GPA_ERR_UNSUPPORTED,
    GPA_ERR_INTERNAL_ERROR,
    GPA_ERR_NOT_COMPRESSED,
};

std::string_view gpaStrError(const GpaError err);
