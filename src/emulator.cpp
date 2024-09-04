// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fmt/core.h>

#include "common/config.h"
#include "common/debug.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#ifdef ENABLE_QT_GUI
#include "qt_gui/memory_patcher.h"
#endif
#include "common/ntapi.h"
#include "common/path_util.h"
#include "common/polyfill_thread.h"
#include "common/scm_rev.h"
#include "common/singleton.h"
#include "common/version.h"
#include "core/file_format/playgo_chunk.h"
#include "core/file_format/psf.h"
#include "core/file_format/splash.h"
#include "core/file_sys/fs.h"
#include "core/libraries/disc_map/disc_map.h"
#include "core/libraries/kernel/thread_management.h"
#include "core/libraries/libc_internal/libc_internal.h"
#include "core/libraries/libs.h"
#include "core/libraries/ngs2/ngs2.h"
#include "core/libraries/rtc/rtc.h"
#include "core/libraries/videoout/video_out.h"
#include "core/linker.h"
#include "core/memory.h"
#include "emulator.h"
#include "video_core/renderdoc.h"

Frontend::WindowSDL* g_window = nullptr;

namespace Core {

Emulator::Emulator() {
    // Read configuration file.
    const auto config_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::load(config_dir / "config.toml");

    // Initialize NT API functions
#ifdef _WIN32
    Common::NtApi::Initialize();
#endif

    // Start logger.
    Common::Log::Initialize();
    Common::Log::Start();
    LOG_INFO(Loader, "Starting shadps4 emulator v{} ", Common::VERSION);
    LOG_INFO(Loader, "Revision {}", Common::g_scm_rev);
    LOG_INFO(Loader, "Branch {}", Common::g_scm_branch);
    LOG_INFO(Loader, "Description {}", Common::g_scm_desc);

    LOG_INFO(Config, "General isNeo: {}", Config::isNeoMode());
    LOG_INFO(Config, "GPU isNullGpu: {}", Config::nullGpu());
    LOG_INFO(Config, "GPU shouldDumpShaders: {}", Config::dumpShaders());
    LOG_INFO(Config, "GPU shouldDumpPM4: {}", Config::dumpPM4());
    LOG_INFO(Config, "GPU vblankDivider: {}", Config::vblankDiv());
    LOG_INFO(Config, "Vulkan gpuId: {}", Config::getGpuId());
    LOG_INFO(Config, "Vulkan vkValidation: {}", Config::vkValidationEnabled());
    LOG_INFO(Config, "Vulkan vkValidationSync: {}", Config::vkValidationSyncEnabled());
    LOG_INFO(Config, "Vulkan vkValidationGpu: {}", Config::vkValidationGpuEnabled());
    LOG_INFO(Config, "Vulkan rdocEnable: {}", Config::isRdocEnabled());
    LOG_INFO(Config, "Vulkan rdocMarkersEnable: {}", Config::vkMarkersEnabled());
    LOG_INFO(Config, "Vulkan crashDiagnostics: {}", Config::vkCrashDiagnosticEnabled());

    // Defer until after logging is initialized.
    memory = Core::Memory::Instance();
    controller = Common::Singleton<Input::GameController>::Instance();
    linker = Common::Singleton<Core::Linker>::Instance();

    // Load renderdoc module.
    VideoCore::LoadRenderDoc();
}

Emulator::~Emulator() {
    const auto config_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::save(config_dir / "config.toml");
}

void Emulator::Run(const std::filesystem::path& file) {
    // Applications expect to be run from /app0 so mount the file's parent path as app0.
    auto* mnt = Common::Singleton<Core::FileSys::MntPoints>::Instance();
    mnt->Mount(file.parent_path(), "/app0");
    // Certain games may use /hostapp as well such as CUSA001100
    mnt->Mount(file.parent_path(), "/hostapp");

    // Loading param.sfo file if exists
    std::string id;
    std::string title;
    std::string app_version;
    std::filesystem::path sce_sys_folder = file.parent_path() / "sce_sys";
    if (std::filesystem::is_directory(sce_sys_folder)) {
        for (const auto& entry : std::filesystem::directory_iterator(sce_sys_folder)) {
            if (entry.path().filename() == "param.sfo") {
                auto* param_sfo = Common::Singleton<PSF>::Instance();
                param_sfo->open(sce_sys_folder.string() + "/param.sfo", {});
                id = std::string(param_sfo->GetString("CONTENT_ID"), 7, 9);
#ifdef ENABLE_QT_GUI
                MemoryPatcher::g_game_serial = id;
#endif
                title = param_sfo->GetString("TITLE");
                LOG_INFO(Loader, "Game id: {} Title: {}", id, title);
                u32 fw_version = param_sfo->GetInteger("SYSTEM_VER");
                app_version = param_sfo->GetString("APP_VER");
                LOG_INFO(Loader, "Fw: {:#x} App Version: {}", fw_version, app_version);
            } else if (entry.path().filename() == "playgo-chunk.dat") {
                auto* playgo = Common::Singleton<PlaygoFile>::Instance();
                auto filepath = sce_sys_folder / "playgo-chunk.dat";
                if (!playgo->Open(filepath)) {
                    LOG_ERROR(Loader, "PlayGo: unable to open file");
                }
            } else if (entry.path().filename() == "pic0.png" ||
                       entry.path().filename() == "pic1.png") {
                auto* splash = Common::Singleton<Splash>::Instance();
                if (splash->IsLoaded()) {
                    continue;
                }
                if (!splash->Open(entry.path().string())) {
                    LOG_ERROR(Loader, "Game splash: unable to open file");
                }
            }
        }
    }

    std::string game_title = fmt::format("{} - {} <{}>", id, title, app_version);
    std::string window_title = "";
    if (Common::isRelease) {
        window_title = fmt::format("shadPS4 v{} | {}", Common::VERSION, game_title);
    } else {
        window_title =
            fmt::format("shadPS4 v{} {} | {}", Common::VERSION, Common::g_scm_desc, game_title);
    }
    window = std::make_unique<Frontend::WindowSDL>(
        Config::getScreenWidth(), Config::getScreenHeight(), controller, window_title);

    g_window = window.get();

    const auto& mount_data_dir = Common::FS::GetUserPath(Common::FS::PathType::GameDataDir) / id;
    if (!std::filesystem::exists(mount_data_dir)) {
        std::filesystem::create_directory(mount_data_dir);
    }
    mnt->Mount(mount_data_dir, "/data"); // should just exist, manually create with game serial
    const auto& mount_temp_dir = Common::FS::GetUserPath(Common::FS::PathType::TempDataDir) / id;
    if (!std::filesystem::exists(mount_temp_dir)) {
        std::filesystem::create_directory(mount_temp_dir);
    }
    mnt->Mount(mount_temp_dir, "/temp0"); // called in app_content ==> stat/mkdir
    mnt->Mount(mount_temp_dir, "/temp");

    const auto& mount_download_dir =
        Common::FS::GetUserPath(Common::FS::PathType::DownloadDir) / id;
    if (!std::filesystem::exists(mount_download_dir)) {
        std::filesystem::create_directory(mount_download_dir);
    }
    mnt->Mount(mount_download_dir, "/download0");

    const auto& mount_captures_dir = Common::FS::GetUserPath(Common::FS::PathType::CapturesDir);
    if (!std::filesystem::exists(mount_captures_dir)) {
        std::filesystem::create_directory(mount_captures_dir);
    }
    VideoCore::SetOutputDir(mount_captures_dir.generic_string(), id);

    // Initialize kernel and library facilities.
    Libraries::Kernel::init_pthreads();
    Libraries::InitHLELibs(&linker->GetHLESymbols());

    // Load the module with the linker
    linker->LoadModule(file);

    // check if we have system modules to load
    LoadSystemModules(file);

    // Load all prx from game's sce_module folder
    std::filesystem::path sce_module_folder = file.parent_path() / "sce_module";
    if (std::filesystem::is_directory(sce_module_folder)) {
        for (const auto& entry : std::filesystem::directory_iterator(sce_module_folder)) {
            LOG_INFO(Loader, "Loading {}", entry.path().string().c_str());
            linker->LoadModule(entry.path());
        }
    }

    // start execution
    std::jthread mainthread =
        std::jthread([this](std::stop_token stop_token) { linker->Execute(); });

    while (window->isOpen()) {
        window->waitEvent();
    }

    std::exit(0);
}

void Emulator::LoadSystemModules(const std::filesystem::path& file) {
    constexpr std::array<SysModules, 9> ModulesToLoad{
        {{"libSceNgs2.sprx", &Libraries::Ngs2::RegisterlibSceNgs2},
         {"libSceFiber.sprx", nullptr},
         {"libSceUlt.sprx", nullptr},
         {"libSceJson.sprx", nullptr},
         {"libSceJson2.sprx", nullptr},
         {"libSceLibcInternal.sprx", &Libraries::LibcInternal::RegisterlibSceLibcInternal},
         {"libSceDiscMap.sprx", &Libraries::DiscMap::RegisterlibSceDiscMap},
         {"libSceRtc.sprx", &Libraries::Rtc::RegisterlibSceRtc},
         {"libSceJpegEnc.sprx", nullptr}},
    };

    std::vector<std::filesystem::path> found_modules;
    const auto& sys_module_path = Common::FS::GetUserPath(Common::FS::PathType::SysModuleDir);
    for (const auto& entry : std::filesystem::directory_iterator(sys_module_path)) {
        found_modules.push_back(entry.path());
    }
    for (const auto& [module_name, init_func] : ModulesToLoad) {
        const auto it = std::ranges::find_if(
            found_modules, [&](const auto& path) { return path.filename() == module_name; });
        if (it != found_modules.end()) {
            LOG_INFO(Loader, "Loading {}", it->string());
            linker->LoadModule(*it);
            continue;
        }
        if (init_func) {
            LOG_INFO(Loader, "Can't Load {} switching to HLE", module_name);
            init_func(&linker->GetHLESymbols());
        } else {
            LOG_INFO(Loader, "No HLE available for {} module", module_name);
        }
    }
}

} // namespace Core
