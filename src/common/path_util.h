// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <vector>

namespace Common::FS {

enum class PathType {
    UserDir,        // Where shadPS4 stores its data.
    LogDir,         // Where log files are stored.
    ScreenshotsDir, // Where screenshots are stored.
    ShaderDir,      // Where shaders are stored.
    PM4Dir,         // Where command lists are stored.
    SaveDataDir,    // Where guest save data is stored.
    TempDataDir,    // Where game temp data is stored.
    GameDataDir,    // Where game data is stored.
    SysModuleDir,   // Where system modules are stored.
    DownloadDir,    // Where downloads/temp files are stored.
    CapturesDir,    // Where rdoc captures are stored.
    CheatsDir,      // Where cheats are stored.
    PatchesDir,     // Where patches are stored.
    AddonsDir,      // Where additional content is stored.
};

constexpr auto PORTABLE_DIR = "user";

// Sub-directories contained within a user data directory
constexpr auto LOG_DIR = "log";
constexpr auto SCREENSHOTS_DIR = "screenshots";
constexpr auto SHADER_DIR = "shader";
constexpr auto PM4_DIR = "pm4";
constexpr auto SAVEDATA_DIR = "savedata";
constexpr auto GAMEDATA_DIR = "data";
constexpr auto TEMPDATA_DIR = "temp";
constexpr auto SYSMODULES_DIR = "sys_modules";
constexpr auto DOWNLOAD_DIR = "download";
constexpr auto CAPTURES_DIR = "captures";
constexpr auto CHEATS_DIR = "cheats";
constexpr auto PATCHES_DIR = "patches";
constexpr auto ADDONS_DIR = "addcont";

// Filenames
constexpr auto LOG_FILE = "shad_log.txt";

/**
 * Validates a given path.
 *
 * A given path is valid if it meets these conditions:
 * - The path is not empty
 * - The path is not too long
 *
 * @param path Filesystem path
 *
 * @returns True if the path is valid, false otherwise.
 */
[[nodiscard]] bool ValidatePath(const std::filesystem::path& path);

/**
 * Converts a filesystem path to a UTF-8 encoded std::string.
 *
 * @param path Filesystem path
 *
 * @returns UTF-8 encoded std::string.
 */
[[nodiscard]] std::string PathToUTF8String(const std::filesystem::path& path);

/**
 * Gets the filesystem path associated with the PathType enum.
 *
 * @param user_path PathType enum
 *
 * @returns The filesystem path associated with the PathType enum.
 */
[[nodiscard]] const std::filesystem::path& GetUserPath(PathType user_path);

/**
 * Gets the filesystem path associated with the PathType enum as a UTF-8 encoded std::string.
 *
 * @param user_path PathType enum
 *
 * @returns The filesystem path associated with the PathType enum as a UTF-8 encoded std::string.
 */
[[nodiscard]] std::string GetUserPathString(PathType user_path);

/**
 * Sets a new filesystem path associated with the PathType enum.
 * If the filesystem object at new_path is not a directory, this function will not do anything.
 *
 * @param user_path PathType enum
 * @param new_path New filesystem path
 */
void SetUserPath(PathType user_path, const std::filesystem::path& new_path);

} // namespace Common::FS
