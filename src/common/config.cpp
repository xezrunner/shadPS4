// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <string>
#include <fmt/core.h>
#include <toml.hpp>
#include "config.h"

namespace Config {

static bool isNeo = false;
static bool isFullscreen = false;
static u32 screenWidth = 1280;
static u32 screenHeight = 720;
static s32 gpuId = -1; // Vulkan physical device index. Set to negative for auto select
static std::string logFilter;
static std::string logType = "async";
static std::string userName = "shadPS4";
static bool useSpecialPad = false;
static int specialPadClass = 1;
static bool isDebugDump = false;
static bool isShowSplash = false;
static bool isNullGpu = false;
static bool shouldCopyGPUBuffers = false;
static bool shouldDumpShaders = false;
static bool shouldDumpPM4 = false;
static u32 vblankDivider = 1;
static bool vkValidation = false;
static bool vkValidationSync = false;
static bool vkValidationGpu = false;
static bool rdocEnable = false;
static bool vkMarkers = false;
static bool vkCrashDiagnostic = false;

// Gui
std::string settings_install_dir = "";
u32 main_window_geometry_x = 400;
u32 main_window_geometry_y = 400;
u32 main_window_geometry_w = 1280;
u32 main_window_geometry_h = 720;
u32 mw_themes = 0;
u32 m_icon_size = 36;
u32 m_icon_size_grid = 69;
u32 m_slider_pos = 0;
u32 m_slider_pos_grid = 0;
u32 m_table_mode = 0;
u32 m_window_size_W = 1280;
u32 m_window_size_H = 720;
std::vector<std::string> m_pkg_viewer;
std::vector<std::string> m_elf_viewer;
std::vector<std::string> m_recent_files;
std::string emulator_language = "en";
// Settings
u32 m_language = 1; // english

bool isNeoMode() {
    return isNeo;
}

bool isFullscreenMode() {
    return isFullscreen;
}

u32 getScreenWidth() {
    return screenWidth;
}

u32 getScreenHeight() {
    return screenHeight;
}

s32 getGpuId() {
    return gpuId;
}

std::string getLogFilter() {
    return logFilter;
}

std::string getLogType() {
    return logType;
}

std::string getUserName() {
    return userName;
}

bool getUseSpecialPad() {
    return useSpecialPad;
}

int getSpecialPadClass() {
    return specialPadClass;
}

bool debugDump() {
    return isDebugDump;
}

bool showSplash() {
    return isShowSplash;
}

bool nullGpu() {
    return isNullGpu;
}

bool copyGPUCmdBuffers() {
    return shouldCopyGPUBuffers;
}

bool dumpShaders() {
    return shouldDumpShaders;
}

bool dumpPM4() {
    return shouldDumpPM4;
}

bool isRdocEnabled() {
    return rdocEnable;
}

bool isMarkersEnabled() {
    return vkMarkers;
}

u32 vblankDiv() {
    return vblankDivider;
}

bool vkValidationEnabled() {
    return vkValidation;
}

bool vkValidationSyncEnabled() {
    return vkValidationSync;
}

bool vkValidationGpuEnabled() {
    return vkValidationGpu;
}

bool vkMarkersEnabled() {
    return vkMarkers || vkCrashDiagnostic; // Crash diagnostic forces markers on
}

bool vkCrashDiagnosticEnabled() {
    return vkCrashDiagnostic;
}

void setGpuId(s32 selectedGpuId) {
    gpuId = selectedGpuId;
}

void setScreenWidth(u32 width) {
    screenWidth = width;
}

void setScreenHeight(u32 height) {
    screenHeight = height;
}

void setDebugDump(bool enable) {
    isDebugDump = enable;
}

void setShowSplash(bool enable) {
    isShowSplash = enable;
}

void setNullGpu(bool enable) {
    isNullGpu = enable;
}

void setCopyGPUCmdBuffers(bool enable) {
    shouldCopyGPUBuffers = enable;
}

void setDumpShaders(bool enable) {
    shouldDumpShaders = enable;
}

void setDumpPM4(bool enable) {
    shouldDumpPM4 = enable;
}

void setVkValidation(bool enable) {
    vkValidation = enable;
}

void setVkSyncValidation(bool enable) {
    vkValidationSync = enable;
}

void setRdocEnabled(bool enable) {
    rdocEnable = enable;
}

void setVblankDiv(u32 value) {
    vblankDivider = value;
}

void setFullscreenMode(bool enable) {
    isFullscreen = enable;
}

void setLanguage(u32 language) {
    m_language = language;
}

void setNeoMode(bool enable) {
    isNeo = enable;
}

void setLogType(const std::string& type) {
    logType = type;
}

void setLogFilter(const std::string& type) {
    logFilter = type;
}

void setUserName(const std::string& type) {
    userName = type;
}

void setUseSpecialPad(bool use) {
    useSpecialPad = use;
}

void setSpecialPadClass(int type) {
    specialPadClass = type;
}

void setMainWindowGeometry(u32 x, u32 y, u32 w, u32 h) {
    main_window_geometry_x = x;
    main_window_geometry_y = y;
    main_window_geometry_w = w;
    main_window_geometry_h = h;
}
void setGameInstallDir(const std::string& dir) {
    settings_install_dir = dir;
}
void setMainWindowTheme(u32 theme) {
    mw_themes = theme;
}
void setIconSize(u32 size) {
    m_icon_size = size;
}
void setIconSizeGrid(u32 size) {
    m_icon_size_grid = size;
}
void setSliderPosition(u32 pos) {
    m_slider_pos = pos;
}
void setSliderPositionGrid(u32 pos) {
    m_slider_pos_grid = pos;
}
void setTableMode(u32 mode) {
    m_table_mode = mode;
}
void setMainWindowWidth(u32 width) {
    m_window_size_W = width;
}
void setMainWindowHeight(u32 height) {
    m_window_size_H = height;
}
void setPkgViewer(const std::vector<std::string>& pkgList) {
    m_pkg_viewer.resize(pkgList.size());
    m_pkg_viewer = pkgList;
}
void setElfViewer(const std::vector<std::string>& elfList) {
    m_elf_viewer.resize(elfList.size());
    m_elf_viewer = elfList;
}
void setRecentFiles(const std::vector<std::string>& recentFiles) {
    m_recent_files.resize(recentFiles.size());
    m_recent_files = recentFiles;
}

void setEmulatorLanguage(std::string language) {
    emulator_language = language;
}

u32 getMainWindowGeometryX() {
    return main_window_geometry_x;
}
u32 getMainWindowGeometryY() {
    return main_window_geometry_y;
}
u32 getMainWindowGeometryW() {
    return main_window_geometry_w;
}
u32 getMainWindowGeometryH() {
    return main_window_geometry_h;
}
std::string getGameInstallDir() {
    return settings_install_dir;
}
u32 getMainWindowTheme() {
    return mw_themes;
}
u32 getIconSize() {
    return m_icon_size;
}
u32 getIconSizeGrid() {
    return m_icon_size_grid;
}
u32 getSliderPosition() {
    return m_slider_pos;
}
u32 getSliderPositionGrid() {
    return m_slider_pos_grid;
}
u32 getTableMode() {
    return m_table_mode;
}
u32 getMainWindowWidth() {
    return m_window_size_W;
}
u32 getMainWindowHeight() {
    return m_window_size_H;
}
std::vector<std::string> getPkgViewer() {
    return m_pkg_viewer;
}
std::vector<std::string> getElfViewer() {
    return m_elf_viewer;
}
std::vector<std::string> getRecentFiles() {
    return m_recent_files;
}

std::string getEmulatorLanguage() {
    return emulator_language;
}

u32 GetLanguage() {
    return m_language;
}
void load(const std::filesystem::path& path) {
    // If the configuration file does not exist, create it and return
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        save(path);
        return;
    }

    toml::value data;

    try {
        data = toml::parse(path);
    } catch (std::exception& ex) {
        fmt::print("Got exception trying to load config file. Exception: {}\n", ex.what());
        return;
    }
    if (data.contains("General")) {
        const toml::value& general = data.at("General");

        isNeo = toml::find_or<bool>(general, "isPS4Pro", false);
        isFullscreen = toml::find_or<bool>(general, "Fullscreen", false);
        logFilter = toml::find_or<std::string>(general, "logFilter", "");
        logType = toml::find_or<std::string>(general, "logType", "sync");
        userName = toml::find_or<std::string>(general, "userName", "shadPS4");
        isShowSplash = toml::find_or<bool>(general, "showSplash", true);
    }

    if (data.contains("Input")) {
        const toml::value& input = data.at("Input");

        useSpecialPad = toml::find_or<bool>(input, "useSpecialPad", false);
        specialPadClass = toml::find_or<int>(input, "specialPadClass", 1);
    }

    if (data.contains("GPU")) {
        const toml::value& gpu = data.at("GPU");

        screenWidth = toml::find_or<int>(gpu, "screenWidth", screenWidth);
        screenHeight = toml::find_or<int>(gpu, "screenHeight", screenHeight);
        isNullGpu = toml::find_or<bool>(gpu, "nullGpu", false);
        shouldCopyGPUBuffers = toml::find_or<bool>(gpu, "copyGPUBuffers", false);
        shouldDumpShaders = toml::find_or<bool>(gpu, "dumpShaders", false);
        shouldDumpPM4 = toml::find_or<bool>(gpu, "dumpPM4", false);
        vblankDivider = toml::find_or<int>(gpu, "vblankDivider", 1);
    }

    if (data.contains("Vulkan")) {
        const toml::value& vk = data.at("Vulkan");

        gpuId = toml::find_or<int>(vk, "gpuId", -1);
        vkValidation = toml::find_or<bool>(vk, "validation", false);
        vkValidationSync = toml::find_or<bool>(vk, "validation_sync", false);
        vkValidationGpu = toml::find_or<bool>(vk, "validation_gpu", true);
        rdocEnable = toml::find_or<bool>(vk, "rdocEnable", false);
        vkMarkers = toml::find_or<bool>(vk, "rdocMarkersEnable", false);
        vkCrashDiagnostic = toml::find_or<bool>(vk, "crashDiagnostic", false);
    }

    if (data.contains("Debug")) {
        const toml::value& debug = data.at("Debug");

        isDebugDump = toml::find_or<bool>(debug, "DebugDump", false);
    }

    if (data.contains("GUI")) {
        const toml::value& gui = data.at("GUI");

        m_icon_size = toml::find_or<int>(gui, "iconSize", 0);
        m_icon_size_grid = toml::find_or<int>(gui, "iconSizeGrid", 0);
        m_slider_pos = toml::find_or<int>(gui, "sliderPos", 0);
        m_slider_pos_grid = toml::find_or<int>(gui, "sliderPosGrid", 0);
        mw_themes = toml::find_or<int>(gui, "theme", 0);
        m_window_size_W = toml::find_or<int>(gui, "mw_width", 0);
        m_window_size_H = toml::find_or<int>(gui, "mw_height", 0);
        settings_install_dir = toml::find_or<std::string>(gui, "installDir", "");
        main_window_geometry_x = toml::find_or<int>(gui, "geometry_x", 0);
        main_window_geometry_y = toml::find_or<int>(gui, "geometry_y", 0);
        main_window_geometry_w = toml::find_or<int>(gui, "geometry_w", 0);
        main_window_geometry_h = toml::find_or<int>(gui, "geometry_h", 0);
        m_pkg_viewer = toml::find_or<std::vector<std::string>>(gui, "pkgDirs", {});
        m_elf_viewer = toml::find_or<std::vector<std::string>>(gui, "elfDirs", {});
        m_recent_files = toml::find_or<std::vector<std::string>>(gui, "recentFiles", {});
        m_table_mode = toml::find_or<int>(gui, "gameTableMode", 0);
        emulator_language = toml::find_or<std::string>(gui, "emulatorLanguage", "en");
    }

    if (data.contains("Settings")) {
        const toml::value& settings = data.at("Settings");

        m_language = toml::find_or<int>(settings, "consoleLanguage", 1);
    }
}
void save(const std::filesystem::path& path) {
    toml::value data;

    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        try {
            data = toml::parse(path);
        } catch (const std::exception& ex) {
            fmt::print("Exception trying to parse config file. Exception: {}\n", ex.what());
            return;
        }
    } else {
        if (error) {
            fmt::print("Filesystem error accessing {} (error: {})\n", path.string(),
                       error.message().c_str());
        }
        fmt::print("Saving new configuration file {}\n", path.string());
    }

    data["General"]["isPS4Pro"] = isNeo;
    data["General"]["Fullscreen"] = isFullscreen;
    data["General"]["logFilter"] = logFilter;
    data["General"]["logType"] = logType;
    data["General"]["userName"] = userName;
    data["General"]["showSplash"] = isShowSplash;
    data["Input"]["useSpecialPad"] = useSpecialPad;
    data["Input"]["specialPadClass"] = specialPadClass;
    data["GPU"]["screenWidth"] = screenWidth;
    data["GPU"]["screenHeight"] = screenHeight;
    data["GPU"]["nullGpu"] = isNullGpu;
    data["GPU"]["copyGPUBuffers"] = shouldCopyGPUBuffers;
    data["GPU"]["dumpShaders"] = shouldDumpShaders;
    data["GPU"]["dumpPM4"] = shouldDumpPM4;
    data["GPU"]["vblankDivider"] = vblankDivider;
    data["Vulkan"]["gpuId"] = gpuId;
    data["Vulkan"]["validation"] = vkValidation;
    data["Vulkan"]["validation_sync"] = vkValidationSync;
    data["Vulkan"]["validation_gpu"] = vkValidationGpu;
    data["Vulkan"]["rdocEnable"] = rdocEnable;
    data["Vulkan"]["rdocMarkersEnable"] = vkMarkers;
    data["Vulkan"]["crashDiagnostic"] = vkCrashDiagnostic;
    data["Debug"]["DebugDump"] = isDebugDump;
    data["GUI"]["theme"] = mw_themes;
    data["GUI"]["iconSize"] = m_icon_size;
    data["GUI"]["sliderPos"] = m_slider_pos;
    data["GUI"]["iconSizeGrid"] = m_icon_size_grid;
    data["GUI"]["sliderPosGrid"] = m_slider_pos_grid;
    data["GUI"]["gameTableMode"] = m_table_mode;
    data["GUI"]["mw_width"] = m_window_size_W;
    data["GUI"]["mw_height"] = m_window_size_H;
    data["GUI"]["installDir"] = settings_install_dir;
    data["GUI"]["geometry_x"] = main_window_geometry_x;
    data["GUI"]["geometry_y"] = main_window_geometry_y;
    data["GUI"]["geometry_w"] = main_window_geometry_w;
    data["GUI"]["geometry_h"] = main_window_geometry_h;
    data["GUI"]["pkgDirs"] = m_pkg_viewer;
    data["GUI"]["elfDirs"] = m_elf_viewer;
    data["GUI"]["recentFiles"] = m_recent_files;
    data["GUI"]["emulatorLanguage"] = emulator_language;

    data["Settings"]["consoleLanguage"] = m_language;

    std::ofstream file(path, std::ios::out);
    file << data;
    file.close();
}

void setDefaultValues() {
    isNeo = false;
    isFullscreen = false;
    screenWidth = 1280;
    screenHeight = 720;
    logFilter = "";
    logType = "async";
    userName = "shadPS4";
    useSpecialPad = false;
    specialPadClass = 1;
    isDebugDump = false;
    isShowSplash = false;
    isNullGpu = false;
    shouldDumpShaders = false;
    shouldDumpPM4 = false;
    vblankDivider = 1;
    vkValidation = false;
    vkValidationSync = false;
    vkValidationGpu = false;
    rdocEnable = false;
    vkMarkers = false;
    vkCrashDiagnostic = false;
    emulator_language = "en";
    m_language = 1;
    gpuId = -1;
}

} // namespace Config
