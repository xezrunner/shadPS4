// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "infamous_second_son_debug_menu.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

#include "common/elf_info.h"
#include "common/logging/log.h"
#include "common/types.h"
#include "core/cpu_patches.h"
#include "core/memory.h"

namespace Core::Devtools::InfamousSecondSonDebugMenu {
namespace {

enum class ActionKind : u32 {
    None,
    SetPower,
    MaxAllPowerRanks,
    LoadWorld,
};

enum class ActionState : u32 {
    Idle,
    Pending,
    Executing,
};

enum class ActionOutcome : u32 {
    None,
    Succeeded,
    PowerOwnerUnavailable,
    PowerRegistryUnavailable,
    NoPowerSkillsFound,
    WorldLoaderUnavailable,
    WorldTransitionRejected,
};

struct PowerChoice {
    const char* label;
    s32 retail_value;
};

struct WorldTransitionContext {
    u64 callback;
    u64 callback_reference;
    u64 callback_argument;
    u32 flags;
    u32 padding_1c;
    u64 resource_list;
    s64 stable_id;
    s32 optional;
    u32 padding_34;
};
static_assert(sizeof(WorldTransitionContext) == 0x38);

struct InstalledWorld {
    std::string name;
    std::uintmax_t package_bytes;
};

// Every guest address and offset below is from _agents/docs/iss/dash-state-spec.md and the
// debug-menu reconstruction (re/debug-menu-reconstruction-2d1ca796), each re-derived from two
// independent code paths.
constexpr uintptr_t ImageBase = 0x01000000;
constexpr uintptr_t MainLoopCallAddress = 0x0129C9E5;
constexpr uintptr_t MainLoopUpdateAddress = 0x0124A280;
constexpr uintptr_t SetPowerAddress = 0x012143D0;
constexpr uintptr_t IsPowerAvailableAddress = 0x011E2E70;
constexpr uintptr_t FindActiveActionByKindAddress = 0x01469440;
constexpr uintptr_t MaxSkillRanksAddress = 0x010E9BB0;
constexpr uintptr_t RebuildPowerActionsAddress = 0x012064E0;
constexpr uintptr_t PowerOwnerAddress = 0x02D0CFE0;
constexpr uintptr_t PowerRegistryManagerAddress = 0x02D0D3A0;
constexpr uintptr_t PowerRegistryReadyAddress = 0x02D0D3A8;
constexpr uintptr_t PlayerStateAddress = 0x01EFACF8;
constexpr uintptr_t WorldManagerAddress = 0x08F111E0;
constexpr uintptr_t InitializeWorldTransitionRequestAddress = 0x01590050;
constexpr uintptr_t SetWorldTransitionRequestNameAddress = 0x01590290;
constexpr uintptr_t DestroyWorldTransitionRequestAddress = 0x010EF650;
constexpr uintptr_t QueueWorldTransitionAddress = 0x0150BA20;
constexpr uintptr_t CreateGuestStringAddress = 0x0160D650;
constexpr uintptr_t CurrentCoreNameAddress = 0x08F2D258;
constexpr uintptr_t CurrentWorldNameAddress = 0x08F2D280;
constexpr s32 LastPowerSkill = 0xDD;

// Dash telemetry globals.
constexpr uintptr_t LogicalTickAddress = 0x01E16294;       // s32, 1440 Hz
constexpr uintptr_t FrameDeltaAddress = 0x01E16298;        // float, guest frame dt
constexpr uintptr_t ActivePowerFamilyAddress = 0x01EFADDC; // s32, 0=Neon 1=Smoke 3=Video 4=Concrete
constexpr uintptr_t ActionIdBitMaskTable = 0x01E17220;
constexpr uintptr_t SmokeDashVTable = 0x01CDC198;
constexpr uintptr_t NeonDashVTable = 0x01D5EAB0;
constexpr s32 SmokeDashTypeId = 0x1AE;
constexpr s32 NeonDashTypeId = 0x1AF;
constexpr s32 DashButtonActionId = 2;
constexpr s32 EndlessDashUpgrade = 0x59;
constexpr s32 NoActiveFamilyTick = -14400;
constexpr float TicksPerSecond = 1440.0f;
constexpr float SmokeTraverseSpeed = 1500.0f; // wu/s, fixed

// SHeroActionController, base `owner`.
constexpr std::size_t OwnerActionOwner = 0x28;
constexpr std::size_t OwnerEntity = 0x38;
constexpr std::size_t OwnerCameraPosition = 0x1A10;
constexpr std::size_t OwnerReadSpan = 0x1A20;

// SGameEntity, base `entity`.
constexpr std::size_t EntityBlockRegion = 0x38;
constexpr std::size_t EntityLocalFrameIndex = 0x0EC;
constexpr std::size_t EntityUsesLocalFrame = 0x0F0;
constexpr std::size_t EntityPosition = 0x120;
constexpr std::size_t EntityXformRow0 = 0x160;
constexpr std::size_t EntityRequestedVelocity = 0x250;
constexpr std::size_t EntityInputState = 0x4B0;
constexpr std::size_t EntityGroundSurface = 0x7B8;
constexpr std::size_t EntityGroundNormal = 0x7F0;
constexpr std::size_t EntityReadSpan = 0x800;

// SDashRuntimeBase, base `dash`. Offsets past +0x0F0 are shared space: Smoke and Neon give the
// same bytes different meanings, so the decode below branches on the vtable, never on the offset.
constexpr std::size_t DashOwnerEntity = 0x038;
constexpr std::size_t DashNeonBridgeTarget = 0x050;
constexpr std::size_t DashAnimationByState = 0x058;
constexpr std::size_t DashActivationTick = 0x0D0;
constexpr std::size_t DashLaunchClip = 0x0D8;
constexpr std::size_t DashNState = 0x0E8;
constexpr std::size_t DashStateEntryTick = 0x0EC;
constexpr std::size_t DashActiveFamilyTick = 0x0F0;
constexpr std::size_t DashRetainedTargetId = 0x120;   // Smoke; -1 when no target is retained
constexpr std::size_t DashRetainedTargetPtr = 0x128;  // Smoke
constexpr std::size_t DashSurfaceContactMode = 0x160; // Neon
constexpr std::size_t DashPathStart = 0x160;          // Smoke
constexpr std::size_t DashPathEnd = 0x170;            // Smoke
constexpr std::size_t DashPathDirection = 0x180;      // Smoke
constexpr std::size_t DashPathLength = 0x190;         // Smoke
constexpr std::size_t DashPathComplete = 0x194;       // Smoke, u8
constexpr std::size_t DashPathMode = 0x1A0;           // Smoke
constexpr std::size_t DashContactNormalZ = 0x1A8;     // Neon
constexpr std::size_t DashTargetSpeed = 0x1E0;        // Neon
constexpr std::size_t DashMovementOverride = 0x4BC;
constexpr std::size_t DashMovementUpdateAborted = 0x518;
constexpr std::size_t DashReadSpan = 0x520;

constexpr std::array<u8, 5> MainLoopCallRetail = {0xE8, 0x96, 0xD8, 0xFA, 0xFF};
constexpr std::size_t HookTrampolineSize = 32;
constexpr std::size_t WorldNameCapacity = 64;
constexpr std::size_t WorldTransitionRequestSize = 0x318;
constexpr u32 FullWorldTransitionFlags = 0x38881; // DevMsnWarpDefault | FullyUnload

// SDashRuntimeBase::nState. State 2 is a zero-duration edge: seeing it in a sample is an anomaly.
enum class DashState : s32 {
    NoRuntime = -2, // no dash object was found or it failed validation
    Inactive = -1,
    LaunchFromGround = 0,
    LaunchFromAir = 1,
    Edge = 2,
    DashRun = 3,
    DashWall = 4,
    DashFloat = 5,
    Exit = 6,
};

enum DashFlag : u32 {
    DashFlagRuntimeFound = 1u << 0,
    DashFlagIsNeon = 1u << 1,
    DashFlagGrounded = 1u << 2,
    DashFlagButtonHeld = 1u << 3,
    DashFlagPathComplete = 1u << 4,
    DashFlagMovementOverride = 1u << 5,
    DashFlagMovementUpdateAborted = 1u << 6, // the state in this sample is last frame's
    DashFlagUsesLocalFrame = 1u << 7,
    DashFlagEndlessDash = 1u << 8,
    DashFlagVelocityValid = 1u << 9,
    DashFlagRetainedTarget = 1u << 10,   // Smoke: the re-plan gate is already closed
    DashFlagNeonBridgeTarget = 1u << 11, // Neon: dash+0x50, the contextual-bridge conjunct
};

// One entry per guest frame, sampled from the main-loop hook.
struct DashSample {
    u64 seq;
    u64 time_us;

    s32 tick;       // g_LogicalTick1440Hz; seconds = ticks / 1440
    float frame_dt; // g_FrameDeltaSeconds

    float position[3];           // entity world position, wu
    float velocity[3];           // finite difference of position, wu/s
    float requested_velocity[3]; // entity+0x250, the only directly-readable velocity
    float camera[3];             // owner+0x1A10; camera-relative input is what the dash consumes
    float ground_normal[3];      // valid only with DashFlagGrounded

    float speed_wu;
    float speed_planar_wu; // |velocity.xy| - the Neon exit gate's quantity
    float facing_yaw;      // atan2(row0.y, row0.x), radians
    float stick_x;
    float stick_y;

    s32 dash_state; // DashState
    s32 state_entry_tick;
    s32 active_family_tick; // -14400 until the first entry into states 2..5
    s32 activation_tick;

    s32 surface_contact_mode; // Neon; -1 when not applicable
    float contact_normal_z;   // Neon; NaN when not applicable
    float target_speed;       // Neon flTargetSpeed; NaN when not applicable
    float path_length;        // Smoke; NaN when not applicable
    s32 path_mode;            // Smoke nPathMode; -1 when not applicable
    float progress;           // Smoke path progress, derived; NaN when undefined

    u64 clip_id;     // animationDataIdByState[dash_state]; u64(-1) = none
    u64 launch_clip; // dash+0xD8; non-zero means the launch clip is still playing

    s32 power; // 0=Neon 1=Smoke 3=Video 4=Concrete
    u32 flags; // DashFlag
};

// Published with a seqlock so the guest thread never blocks on the UI: invalidate, fill,
// republish. A reader that sees 0 or a changed sequence retries, so it can never accept a
// half-written entry.
struct DashSlot {
    std::atomic<u64> seq;
    DashSample sample;
};

constexpr std::size_t DashRingCapacity = 256;
constexpr std::size_t DashFeedRows = 64;

bool hooks_installed = false;
bool window_open = false;
bool main_loop_entered = false;
uintptr_t eboot_base_address = 0;

ActionKind pending_action = ActionKind::None;
s32 pending_power = 0;
std::array<char, WorldNameCapacity> pending_world{};
std::atomic<ActionState> action_state = ActionState::Idle;
std::atomic<ActionOutcome> action_outcome = ActionOutcome::None;

std::vector<InstalledWorld> installed_worlds;
bool installed_worlds_scanned = false;

std::array<DashSlot, DashRingCapacity> dash_ring{};
std::atomic<u64> dash_sequence{0};
std::atomic<bool> dash_feed_enabled{false};

// The endless-dash upgrade changes the meaning of every Neon duration, so it is latched once per
// capture on the guest thread rather than queried every frame.
std::atomic<bool> endless_dash_known{false};
std::atomic<bool> endless_dash{false};

// Finite-difference state. `previous_entity` is atomic only so enabling the feed can invalidate it
// from another thread; the other two are written and read by the guest thread alone.
std::atomic<uintptr_t> previous_entity{0};
float previous_position[3]{};
float previous_dt = 0.0f;

constexpr std::array PowerChoices = {
    PowerChoice{"Smoke", 1},
    PowerChoice{"Neon", 0},
    PowerChoice{"Video", 3},
    PowerChoice{"Concrete", 4},
};

template <typename Function>
Function GuestFunction(uintptr_t address) {
    return reinterpret_cast<Function>(eboot_base_address + address - ImageBase);
}

uintptr_t GuestAddress(uintptr_t address) {
    return eboot_base_address + address - ImageBase;
}

u64 NowMicroseconds() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}

bool IsReadable(uintptr_t address, std::size_t size) {
    if (address == 0 || size == 0 || address > std::numeric_limits<uintptr_t>::max() - size) {
        return false;
    }

    auto* memory = Core::Memory::Instance();
    if (!memory->IsValidMapping(address, size)) {
        return false;
    }

    void* start = nullptr;
    void* end = nullptr;
    u32 protection = 0;
    if (memory->QueryProtection(address, &start, &end, &protection) != 0) {
        return false;
    }

    const auto range_end = reinterpret_cast<uintptr_t>(end);
    return (protection & static_cast<u32>(MemoryProt::CpuRead)) != 0 && address + size <= range_end;
}

using SetPower = void PS4_SYSV_ABI (*)(void* owner, s32 retail_value);
using IsPowerAvailable = u8 PS4_SYSV_ABI (*)(void* state, s32 power, s32 parameter);
using FindActiveActionByKind = void* PS4_SYSV_ABI (*)(void* action_owner, s32 type_id);
using MaxSkillRanks = void PS4_SYSV_ABI (*)(void* unused, s32 skill);
using RebuildPowerActions = void PS4_SYSV_ABI (*)(void* owner);
using InitializeWorldTransitionRequest = void PS4_SYSV_ABI (*)(void* request);
using SetWorldTransitionRequestName = void PS4_SYSV_ABI (*)(void* request, u16** name);
using DestroyWorldTransitionRequest = void PS4_SYSV_ABI (*)(void* request);
using QueueWorldTransition = u8 PS4_SYSV_ABI (*)(void* world_manager, void* request,
                                                 WorldTransitionContext* context);
using CreateGuestString = void PS4_SYSV_ABI (*)(u16** result, const char* text, u64 length);

std::string ToLower(std::string_view text) {
    std::string lowered{text};
    std::ranges::transform(lowered, lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

void RefreshInstalledWorlds() {
    installed_worlds.clear();
    installed_worlds_scanned = true;

    const auto cache = Common::ElfInfo::Instance().GetGameFolder() / "art" / "cache";
    std::error_code error;
    std::filesystem::directory_iterator entry{cache, error};
    const std::filesystem::directory_iterator end;
    while (!error && entry != end) {
        std::error_code item_error;
        if (entry->is_regular_file(item_error)) {
            const auto lower = ToLower(entry->path().filename().string());
            if (lower.starts_with("world_") && lower.ends_with(".xpps")) {
                const auto bytes = entry->file_size(item_error);
                if (!item_error) {
                    installed_worlds.push_back({lower.substr(0, lower.size() - 5), bytes});
                }
            }
        }
        entry.increment(error);
    }

    std::ranges::sort(installed_worlds, {}, &InstalledWorld::package_bytes);
}

std::string ReadGuestString(uintptr_t slot_address) {
    const uintptr_t slot = GuestAddress(slot_address);
    if (!IsReadable(slot, sizeof(uintptr_t))) {
        return {};
    }
    const uintptr_t value = *reinterpret_cast<const uintptr_t*>(slot);
    if (!IsReadable(value, sizeof(u16) + 1)) {
        return {};
    }

    std::string result;
    const uintptr_t text = value + sizeof(u16);
    for (std::size_t i = 0; i < WorldNameCapacity - 1 && IsReadable(text + i, 1); ++i) {
        const char character = *reinterpret_cast<const char*>(text + i);
        if (character == '\0') {
            break;
        }
        result.push_back(character);
    }
    return result;
}

ActionOutcome ExecutePower(s32 retail_value) {
    const uintptr_t owner_slot = GuestAddress(PowerOwnerAddress);
    if (!IsReadable(owner_slot, sizeof(uintptr_t))) {
        return ActionOutcome::PowerOwnerUnavailable;
    }

    const uintptr_t owner = *reinterpret_cast<const uintptr_t*>(owner_slot);
    if (!IsReadable(owner, 0x40)) {
        return ActionOutcome::PowerOwnerUnavailable;
    }

    GuestFunction<SetPower>(SetPowerAddress)(reinterpret_cast<void*>(owner), retail_value);
    return ActionOutcome::Succeeded;
}

ActionOutcome ExecuteMaxAllPowerRanks() {
    const uintptr_t owner_slot = GuestAddress(PowerOwnerAddress);
    const uintptr_t manager_slot = GuestAddress(PowerRegistryManagerAddress);
    const uintptr_t ready_address = GuestAddress(PowerRegistryReadyAddress);
    if (!IsReadable(owner_slot, sizeof(uintptr_t)) ||
        !IsReadable(manager_slot, sizeof(uintptr_t)) ||
        !IsReadable(ready_address, sizeof(uintptr_t)) ||
        *reinterpret_cast<const uintptr_t*>(ready_address) == 0) {
        return ActionOutcome::PowerRegistryUnavailable;
    }

    const uintptr_t owner = *reinterpret_cast<const uintptr_t*>(owner_slot);
    const uintptr_t manager = *reinterpret_cast<const uintptr_t*>(manager_slot);
    constexpr std::size_t RegistryBytes = 0x108 + (LastPowerSkill + 1) * sizeof(uintptr_t);
    if (!IsReadable(owner, 0x40) || !IsReadable(manager, RegistryBytes)) {
        return ActionOutcome::PowerRegistryUnavailable;
    }

    const auto max_skill = GuestFunction<MaxSkillRanks>(MaxSkillRanksAddress);
    u32 changed = 0;
    for (s32 skill = 0; skill <= LastPowerSkill; ++skill) {
        const uintptr_t entry =
            *reinterpret_cast<const uintptr_t*>(manager + 0x108 + skill * sizeof(uintptr_t));
        // NOTE: Runtime validation proved this is only a sparse presence table; the record's
        // first dword is not the skill ID.
        if (entry == 0) {
            continue;
        }
        max_skill(nullptr, skill);
        ++changed;
    }
    if (changed == 0) {
        return ActionOutcome::NoPowerSkillsFound;
    }

    GuestFunction<RebuildPowerActions>(RebuildPowerActionsAddress)(reinterpret_cast<void*>(owner));
    LOG_INFO(Debug, "Maxed {} registered Second Son power skills", changed);
    return ActionOutcome::Succeeded;
}

ActionOutcome ExecuteWorldTransition(std::string_view world) {
    const uintptr_t manager = GuestAddress(WorldManagerAddress);
    const uintptr_t current_core_slot = GuestAddress(CurrentCoreNameAddress);
    if (!IsReadable(manager, 0x90) || !IsReadable(current_core_slot, sizeof(uintptr_t))) {
        return ActionOutcome::WorldLoaderUnavailable;
    }

    auto* current_core = *reinterpret_cast<u16* const*>(current_core_slot);
    if (!IsReadable(reinterpret_cast<uintptr_t>(current_core), sizeof(u16) + 1)) {
        return ActionOutcome::WorldLoaderUnavailable;
    }

    alignas(32) std::array<u8, WorldTransitionRequestSize> request{};
    const auto initialize = GuestFunction<InitializeWorldTransitionRequest>(
        InitializeWorldTransitionRequestAddress);
    const auto destroy =
        GuestFunction<DestroyWorldTransitionRequest>(DestroyWorldTransitionRequestAddress);
    initialize(request.data());

    // The initializer owns four empty-string references. The retail callers replace +0xB0 with
    // the current core and +0xB8 with the requested world before queueing the deep copy.
    if (*current_core != 0) {
        ++*current_core;
    }
    *reinterpret_cast<u16**>(request.data() + 0xB0) = current_core;

    u16* target = nullptr;
    GuestFunction<CreateGuestString>(CreateGuestStringAddress)(&target, world.data(),
                                                              std::numeric_limits<u64>::max());
    if (!IsReadable(reinterpret_cast<uintptr_t>(target), sizeof(u16) + world.size() + 1)) {
        destroy(request.data());
        return ActionOutcome::WorldLoaderUnavailable;
    }

    // SetWorldTransitionRequestName retains once for +0xA8. The creator's original reference is
    // transferred to +0xB8; the request destructor releases both after the queued job deep-copies.
    GuestFunction<SetWorldTransitionRequestName>(SetWorldTransitionRequestNameAddress)(
        request.data(), &target);
    *reinterpret_cast<u16**>(request.data() + 0xB8) = target;

    WorldTransitionContext context{
        .callback = 0,
        .callback_reference = 0,
        .callback_argument = 0,
        .flags = FullWorldTransitionFlags,
        .padding_1c = 0,
        .resource_list = 0,
        .stable_id = -1,
        .optional = -1,
        .padding_34 = 0,
    };
    const u8 queued = GuestFunction<QueueWorldTransition>(QueueWorldTransitionAddress)(
        reinterpret_cast<void*>(manager), request.data(), &context);
    destroy(request.data());

    if (queued != 0) {
        LOG_INFO(Debug, "Queued full Second Son world transition to '{}'", world);
        return ActionOutcome::Succeeded;
    }
    return ActionOutcome::WorldTransitionRejected;
}

// Queues an action for the guest game thread; the payload (pending_power / pending_world) must be
// set before calling. Returns false when an action is already in flight.
bool QueueAction(ActionKind kind) {
    if (action_state.load(std::memory_order_acquire) != ActionState::Idle) {
        return false;
    }

    pending_action = kind;
    action_outcome.store(ActionOutcome::None, std::memory_order_relaxed);
    action_state.store(ActionState::Pending, std::memory_order_release);
    return true;
}

void DrainPendingAction() {
    auto expected = ActionState::Pending;
    if (!action_state.compare_exchange_strong(expected, ActionState::Executing,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
        return;
    }

    ActionOutcome outcome = ActionOutcome::None;
    switch (pending_action) {
    case ActionKind::SetPower:
        outcome = ExecutePower(pending_power);
        break;
    case ActionKind::MaxAllPowerRanks:
        outcome = ExecuteMaxAllPowerRanks();
        break;
    case ActionKind::LoadWorld:
        outcome = ExecuteWorldTransition(pending_world.data());
        break;
    case ActionKind::None:
        break;
    }

    action_outcome.store(outcome, std::memory_order_release);
    action_state.store(ActionState::Idle, std::memory_order_release);
}

// Mirrors GetEntityWorldPositionAtAttachment (0x015ACC80). Replicated rather than called: the guest
// function returns a float4 in XMM0 behind an `undefined8` prototype, and having every intermediate
// inspectable is worth more here than reusing the code.
bool WorldPosition(uintptr_t entity, float out[3]) {
    const auto* local = reinterpret_cast<const float*>(entity + EntityPosition);
    const uintptr_t region = *reinterpret_cast<const uintptr_t*>(entity + EntityBlockRegion);
    if (region == 0 || *reinterpret_cast<const u8*>(entity + EntityUsesLocalFrame) == 0) {
        out[0] = local[0];
        out[1] = local[1];
        out[2] = local[2];
        return true;
    }

    if (!IsReadable(region, 0x290)) {
        return false;
    }
    const uintptr_t table = *reinterpret_cast<const uintptr_t*>(region + 0x288);
    const auto index =
        static_cast<std::size_t>(*reinterpret_cast<const s32*>(entity + EntityLocalFrameIndex));
    const uintptr_t frame = table + 0x40 + index * 0x40;
    if (!IsReadable(frame, 0x40)) {
        return false;
    }

    // Rows at +0x00/+0x10/+0x20, translation at +0x30.
    const auto* m = reinterpret_cast<const float*>(frame);
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = local[0] * m[i] + local[1] * m[4 + i] + local[2] * m[8 + i] + m[12 + i];
    }
    return true;
}

// Smoke stores no progress scalar, so project the world position onto the planned path.
float SmokePathProgress(uintptr_t dash, const float position[3]) {
    const auto* start = reinterpret_cast<const float*>(dash + DashPathStart);
    const auto* end = reinterpret_cast<const float*>(dash + DashPathEnd);
    const auto* direction = reinterpret_cast<const float*>(dash + DashPathDirection);
    float total = 0.0f;
    float current = 0.0f;
    for (std::size_t i = 0; i < 3; ++i) {
        total += direction[i] * (end[i] - start[i]);
        current += direction[i] * (position[i] - start[i]);
    }
    return std::abs(total) < 1e-3f ? std::numeric_limits<float>::quiet_NaN() : current / total;
}

// Fills the per-power half of the record. `neon` comes from the vtable because Smoke and Neon
// disagree about what the bytes past +0x0F0 mean - +0x1A0 in particular is Smoke's nPathMode and
// Neon's contact plane, so decoding by offset alone would silently produce plausible nonsense.
void DecodeDashRuntime(uintptr_t dash, bool neon, DashSample& sample) {
    sample.flags |= DashFlagRuntimeFound;
    sample.dash_state = *reinterpret_cast<const s32*>(dash + DashNState);
    sample.state_entry_tick = *reinterpret_cast<const s32*>(dash + DashStateEntryTick);
    sample.active_family_tick = *reinterpret_cast<const s32*>(dash + DashActiveFamilyTick);
    sample.activation_tick = *reinterpret_cast<const s32*>(dash + DashActivationTick);
    sample.launch_clip = *reinterpret_cast<const u64*>(dash + DashLaunchClip);
    if (sample.dash_state >= 0) {
        sample.clip_id = *reinterpret_cast<const u64*>(dash + DashAnimationByState +
                                                       sample.dash_state * sizeof(u64));
    }
    if (*reinterpret_cast<const u8*>(dash + DashMovementOverride) != 0) {
        sample.flags |= DashFlagMovementOverride;
    }
    // NOTE: bMovementUpdateAborted means SelectState returned before classifying anything, so the
    // state in this sample is the previous frame's. Reading it as live is the easiest way to draw a
    // wrong conclusion from a capture, which is why it is a first-class flag.
    if (*reinterpret_cast<const u8*>(dash + DashMovementUpdateAborted) != 0) {
        sample.flags |= DashFlagMovementUpdateAborted;
    }

    if (neon) {
        sample.flags |= DashFlagIsNeon;
        sample.surface_contact_mode = *reinterpret_cast<const s32*>(dash + DashSurfaceContactMode);
        sample.contact_normal_z = *reinterpret_cast<const float*>(dash + DashContactNormalZ);
        sample.target_speed = *reinterpret_cast<const float*>(dash + DashTargetSpeed);
        if (*reinterpret_cast<const uintptr_t*>(dash + DashNeonBridgeTarget) != 0) {
            sample.flags |= DashFlagNeonBridgeTarget;
        }
        return;
    }

    sample.path_length = *reinterpret_cast<const float*>(dash + DashPathLength);
    sample.path_mode = *reinterpret_cast<const s32*>(dash + DashPathMode);
    sample.progress = SmokePathProgress(dash, sample.position);
    // NOTE: bPathComplete is a u8; the game tests it as a char.
    if (*reinterpret_cast<const u8*>(dash + DashPathComplete) != 0) {
        sample.flags |= DashFlagPathComplete;
    }
    if (*reinterpret_cast<const s32*>(dash + DashRetainedTargetId) != -1 &&
        *reinterpret_cast<const uintptr_t*>(dash + DashRetainedTargetPtr) != 0) {
        sample.flags |= DashFlagRetainedTarget;
    }
}

// Runs once per guest frame from the main-loop hook. Memory reads plus one list walk to find the
// live dash runtime, so it adds no new execution context.
void SampleDashState() {
    if (!dash_feed_enabled.load(std::memory_order_relaxed)) {
        return;
    }

    const uintptr_t owner_slot = GuestAddress(PowerOwnerAddress);
    if (!IsReadable(owner_slot, sizeof(uintptr_t))) {
        return;
    }
    const uintptr_t owner = *reinterpret_cast<const uintptr_t*>(owner_slot);
    if (!IsReadable(owner, OwnerReadSpan)) {
        return;
    }
    const uintptr_t entity = *reinterpret_cast<const uintptr_t*>(owner + OwnerEntity);
    if (!IsReadable(entity, EntityReadSpan)) {
        return;
    }

    DashSample sample{};
    sample.time_us = NowMicroseconds();
    sample.dash_state = static_cast<s32>(DashState::NoRuntime);
    sample.surface_contact_mode = -1;
    sample.path_mode = -1;
    sample.contact_normal_z = std::numeric_limits<float>::quiet_NaN();
    sample.target_speed = std::numeric_limits<float>::quiet_NaN();
    sample.path_length = std::numeric_limits<float>::quiet_NaN();
    sample.progress = std::numeric_limits<float>::quiet_NaN();
    sample.clip_id = std::numeric_limits<u64>::max();
    sample.power = -1;

    if (const uintptr_t address = GuestAddress(LogicalTickAddress); IsReadable(address, 4)) {
        sample.tick = *reinterpret_cast<const s32*>(address);
    }
    if (const uintptr_t address = GuestAddress(FrameDeltaAddress); IsReadable(address, 4)) {
        sample.frame_dt = *reinterpret_cast<const float*>(address);
    }
    if (const uintptr_t address = GuestAddress(ActivePowerFamilyAddress); IsReadable(address, 4)) {
        sample.power = *reinterpret_cast<const s32*>(address);
    }

    if (!WorldPosition(entity, sample.position)) {
        return;
    }
    if (*reinterpret_cast<const u8*>(entity + EntityUsesLocalFrame) != 0) {
        sample.flags |= DashFlagUsesLocalFrame;
    }

    // NOTE: entity+0x210 looks like the velocity and reads exactly 0.0 through a straight Smoke
    // dash, so the feed differences the world position and keeps +0x250 as the direct cross-check.
    // `previous_dt` is the dt that was live while the entity travelled between the two samples.
    if (previous_entity.load(std::memory_order_relaxed) == entity && previous_dt > 0.0f) {
        for (std::size_t i = 0; i < 3; ++i) {
            sample.velocity[i] = (sample.position[i] - previous_position[i]) / previous_dt;
        }
        sample.flags |= DashFlagVelocityValid;
    }
    previous_entity.store(entity, std::memory_order_relaxed);
    std::copy_n(sample.position, 3, previous_position);
    previous_dt = sample.frame_dt;

    sample.speed_wu = std::sqrt(sample.velocity[0] * sample.velocity[0] +
                                sample.velocity[1] * sample.velocity[1] +
                                sample.velocity[2] * sample.velocity[2]);
    sample.speed_planar_wu = std::sqrt(sample.velocity[0] * sample.velocity[0] +
                                       sample.velocity[1] * sample.velocity[1]);
    std::copy_n(reinterpret_cast<const float*>(entity + EntityRequestedVelocity), 3,
                sample.requested_velocity);
    std::copy_n(reinterpret_cast<const float*>(owner + OwnerCameraPosition), 3, sample.camera);

    const auto* row0 = reinterpret_cast<const float*>(entity + EntityXformRow0);
    sample.facing_yaw = std::atan2(row0[1], row0[0]);

    if (*reinterpret_cast<const uintptr_t*>(entity + EntityGroundSurface) != 0) {
        sample.flags |= DashFlagGrounded;
        std::copy_n(reinterpret_cast<const float*>(entity + EntityGroundNormal), 3,
                    sample.ground_normal);
    }

    const uintptr_t input = *reinterpret_cast<const uintptr_t*>(entity + EntityInputState);
    if (IsReadable(input, 0x24)) {
        sample.stick_x = *reinterpret_cast<const float*>(input + 0x08);
        sample.stick_y = *reinterpret_cast<const float*>(input + 0x0C);
        // GetActionIdBitMask (0x0115DDE0) is `table[id]` and nothing else, so read the table.
        const uintptr_t mask_address =
            GuestAddress(ActionIdBitMaskTable + DashButtonActionId * sizeof(u32));
        if (IsReadable(mask_address, sizeof(u32)) &&
            (*reinterpret_cast<const u32*>(input + 0x20) &
             *reinterpret_cast<const u32*>(mask_address)) != 0) {
            sample.flags |= DashFlagButtonHeld;
        }
    }

    if (!endless_dash_known.exchange(true, std::memory_order_relaxed)) {
        endless_dash.store(GuestFunction<IsPowerAvailable>(IsPowerAvailableAddress)(
                               reinterpret_cast<void*>(GuestAddress(PlayerStateAddress)),
                               EndlessDashUpgrade, 0) != 0,
                           std::memory_order_relaxed);
    }
    if (endless_dash.load(std::memory_order_relaxed)) {
        sample.flags |= DashFlagEndlessDash;
    }

    const s32 type_id = sample.power == 1   ? SmokeDashTypeId
                        : sample.power == 0 ? NeonDashTypeId
                                            : -1;
    const uintptr_t action_owner = *reinterpret_cast<const uintptr_t*>(owner + OwnerActionOwner);
    if (type_id >= 0 && IsReadable(action_owner, 0x58)) {
        const auto dash = reinterpret_cast<uintptr_t>(GuestFunction<FindActiveActionByKind>(
            FindActiveActionByKindAddress)(reinterpret_cast<void*>(action_owner), type_id));
        if (IsReadable(dash, DashReadSpan)) {
            // The list walk can only return an object of the requested type, but the three checks
            // are cheap and a mis-decode here would look entirely plausible in a capture.
            const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(dash);
            const bool neon = vtable == GuestAddress(NeonDashVTable);
            const s32 state = *reinterpret_cast<const s32*>(dash + DashNState);
            if ((neon || vtable == GuestAddress(SmokeDashVTable)) && state >= -1 && state <= 6 &&
                *reinterpret_cast<const uintptr_t*>(dash + DashOwnerEntity) == entity) {
                DecodeDashRuntime(dash, neon, sample);
            }
        }
    }

    const u64 sequence = dash_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    auto& slot = dash_ring[(sequence - 1) % DashRingCapacity];
    slot.seq.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    sample.seq = sequence;
    slot.sample = sample;
    slot.seq.store(sequence, std::memory_order_release);
}

std::size_t SnapshotDashSamples(DashSample* out, std::size_t max) {
    const u64 published = dash_sequence.load(std::memory_order_acquire);
    std::size_t written = 0;
    for (u64 sequence = published; sequence > 0 && written < max; --sequence) {
        if (published - sequence >= DashRingCapacity) {
            break;
        }
        const auto& slot = dash_ring[(sequence - 1) % DashRingCapacity];
        if (slot.seq.load(std::memory_order_acquire) != sequence) {
            continue;
        }
        const DashSample sample = slot.sample;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (slot.seq.load(std::memory_order_relaxed) != sequence) {
            continue; // the writer lapped us mid-read
        }
        out[written++] = sample;
    }
    return written;
}

void PS4_SYSV_ABI DrainPendingActionHook() {
    if (!main_loop_entered) {
        main_loop_entered = true;
        LOG_INFO(Debug, "Second Son guest main-loop executor is active");
    }
    DrainPendingAction();
    SampleDashState();
}

bool VerifyMainLoopCall(uintptr_t eboot_base) {
    const auto* target = reinterpret_cast<const u8*>(eboot_base + MainLoopCallAddress - ImageBase);
    if (std::memcmp(target, MainLoopCallRetail.data(), MainLoopCallRetail.size()) == 0) {
        return true;
    }
    LOG_ERROR(Debug, "Second Son main-loop call mismatch at {:#x}", MainLoopCallAddress);
    return false;
}

bool InstallMainLoopHook(uintptr_t eboot_base) {
    auto* trampoline = static_cast<u8*>(
        AllocatePatchTrampoline(reinterpret_cast<void*>(eboot_base), HookTrampolineSize));
    if (trampoline == nullptr) {
        LOG_ERROR(Debug, "Could not reserve a Second Son main-loop trampoline");
        return false;
    }

    std::array<u8, HookTrampolineSize> code{};
    std::fill(code.begin(), code.end(), 0x90);
    std::size_t cursor = 0;
    const auto emit = [&](std::initializer_list<u8> bytes) {
        std::copy(bytes.begin(), bytes.end(), code.begin() + cursor);
        cursor += bytes.size();
    };
    const auto emit_address = [&](uintptr_t address) {
        std::memcpy(code.data() + cursor, &address, sizeof(address));
        cursor += sizeof(address);
    };

    emit({0x48, 0x83, 0xEC, 0x08}); // sub rsp, 8
    emit({0x48, 0xB8});             // mov rax, host hook
    emit_address(reinterpret_cast<uintptr_t>(&DrainPendingActionHook));
    emit({0xFF, 0xD0});             // call rax
    emit({0x48, 0x83, 0xC4, 0x08}); // add rsp, 8
    emit({0x48, 0xB8});             // mov rax, original guest update
    emit_address(GuestAddress(MainLoopUpdateAddress));
    emit({0xFF, 0xE0}); // jmp rax
    std::memcpy(trampoline, code.data(), code.size());

    auto* callsite = reinterpret_cast<u8*>(eboot_base + MainLoopCallAddress - ImageBase);
    const auto displacement =
        reinterpret_cast<intptr_t>(trampoline) - (reinterpret_cast<intptr_t>(callsite) + 5);
    if (displacement < std::numeric_limits<s32>::min() ||
        displacement > std::numeric_limits<s32>::max()) {
        LOG_ERROR(Debug, "Second Son main-loop trampoline is out of rel32 range");
        return false;
    }

    std::array<u8, 5> call{0xE8};
    const s32 relative = static_cast<s32>(displacement);
    std::memcpy(call.data() + 1, &relative, sizeof(relative));
    std::memcpy(callsite, call.data(), call.size());
    return true;
}

const char* OutcomeText(ActionOutcome outcome) {
    switch (outcome) {
    case ActionOutcome::None:
        return nullptr;
    case ActionOutcome::Succeeded:
        return "Last action succeeded.";
    case ActionOutcome::PowerOwnerUnavailable:
        return "Power change rejected: the active hero power owner is unavailable.";
    case ActionOutcome::PowerRegistryUnavailable:
        return "Power ranks were not changed: the hero/upgrade registry is not ready.";
    case ActionOutcome::NoPowerSkillsFound:
        return "Power ranks were not changed: no registered power skills were found.";
    case ActionOutcome::WorldLoaderUnavailable:
        return "World load rejected: the retail world manager or request strings are not ready.";
    case ActionOutcome::WorldTransitionRejected:
        return "World load rejected: another retail transition is already in flight.";
    }
    return nullptr;
}

void DrawPowers() {
    if (!ImGui::CollapsingHeader("Powers", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const bool busy = action_state.load(std::memory_order_acquire) != ActionState::Idle;
    ImGui::BeginDisabled(busy);
    for (const auto& power : PowerChoices) {
        if (ImGui::Button(power.label)) {
            pending_power = power.retail_value;
            QueueAction(ActionKind::SetPower);
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Max all power ranks")) {
        QueueAction(ActionKind::MaxAllPowerRanks);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Uses the retail progression setters; the maxed ranks can be written to the save.");
    }
}

void DrawWorldLoader() {
    if (!ImGui::CollapsingHeader("World loading", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (!installed_worlds_scanned) {
        RefreshInstalledWorlds();
    }

    const auto current = ToLower(ReadGuestString(CurrentWorldNameAddress));
    ImGui::Text("Current world: %s", current.empty() ? "not loaded" : current.c_str());
    ImGui::TextDisabled(
        "Uses the retail mission-warp job and FullyUnload; the old world is released.");
    if (ImGui::SmallButton("Rescan world packages")) {
        RefreshInstalledWorlds();
    }

    if (installed_worlds.empty()) {
        ImGui::TextDisabled("No art/cache/world_*.xpps packages were found.");
        return;
    }

    const bool busy = action_state.load(std::memory_order_acquire) != ActionState::Idle;
    if (!ImGui::BeginTable("installed_worlds", 3,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_Resizable)) {
        return;
    }
    ImGui::TableSetupColumn("World");
    ImGui::TableSetupColumn("MiB");
    ImGui::TableSetupColumn("Action");
    ImGui::TableHeadersRow();
    for (const auto& world : installed_worlds) {
        ImGui::PushID(world.name.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(world.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", static_cast<double>(world.package_bytes) / (1024.0 * 1024.0));
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(busy || current == world.name);
        if (ImGui::SmallButton("Load") && world.name.size() < pending_world.size()) {
            pending_world.fill('\0');
            std::ranges::copy(world.name, pending_world.begin());
            QueueAction(ActionKind::LoadWorld);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndTable();
}

const char* DashStateName(s32 state) {
    switch (static_cast<DashState>(state)) {
    case DashState::NoRuntime:
        return "no runtime";
    case DashState::Inactive:
        return "Inactive";
    case DashState::LaunchFromGround:
        return "LaunchFromGround";
    case DashState::LaunchFromAir:
        return "LaunchFromAir";
    case DashState::Edge:
        return "(edge)";
    case DashState::DashRun:
        return "DashRun";
    case DashState::DashWall:
        return "DashWall";
    case DashState::DashFloat:
        return "DashFloat";
    case DashState::Exit:
        return "Exit";
    }
    return "?";
}

const char* PowerName(s32 power) {
    switch (power) {
    case 0:
        return "Neon";
    case 1:
        return "Smoke";
    case 3:
        return "Video";
    case 4:
        return "Concrete";
    }
    return "-";
}

bool TickSeconds(s32 tick, s32 origin, float& seconds) {
    if (origin == NoActiveFamilyTick) {
        return false; // not latched yet
    }
    seconds = static_cast<float>(tick - origin) / TicksPerSecond;
    return true;
}

void DashFlagLetters(u32 flags, char out[16]) {
    // Runtime, Neon, Grounded, Held, PathComplete, Override, Aborted, LocalFrame, Endless,
    // VelocityValid, retainedTarget, Bridge - in DashFlag bit order.
    constexpr std::string_view Letters = "RNGHPOALEVTB";
    std::size_t written = 0;
    for (std::size_t bit = 0; bit < Letters.size(); ++bit) {
        if ((flags & (1u << bit)) != 0) {
            out[written++] = Letters[bit];
        }
    }
    out[written] = '\0';
}

// One lamp per gate window, each carrying the live quantity rather than only a colour: knowing a
// gate is shut is far less useful than knowing which conjunct is holding it shut.
void DrawGateLamp(const char* label, bool open, const std::string& detail) {
    ImGui::SameLine();
    ImGui::TextColored(open ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       "[%s %s]", label, detail.c_str());
}

void DrawDashGates(const DashSample& live) {
    const bool neon = (live.flags & DashFlagIsNeon) != 0;
    const bool held = (live.flags & DashFlagButtonHeld) != 0;
    float active = 0.0f;
    const bool active_valid = TickSeconds(live.tick, live.active_family_tick, active);
    float since_activation = 0.0f;
    TickSeconds(live.tick, live.activation_tick, since_activation);

    ImGui::TextUnformatted("Gates");
    if (!neon) {
        const bool retained = (live.flags & DashFlagRetainedTarget) != 0;
        const bool open = live.dash_state <= 1 && since_activation <= 0.15f && !retained;
        DrawGateLamp("replan", open,
                     retained
                         ? "closed by retained target"
                         : fmt::format("{:.0f} ms left", (0.15f - since_activation) * 1000.0f));
        return;
    }

    const bool bridge_reachable = live.surface_contact_mode == 1;
    DrawGateLamp("bridge",
                 bridge_reachable && since_activation >= 0.35f &&
                     (live.flags & DashFlagNeonBridgeTarget) != 0 &&
                     (live.flags & DashFlagMovementOverride) == 0,
                 bridge_reachable ? fmt::format("{:.2f} s", since_activation) : "n/a");
    DrawGateLamp("speed-drop", active_valid && active >= 0.4f && !held,
                 fmt::format("target {:.0f}", live.target_speed));

    const bool endless = (live.flags & DashFlagEndlessDash) != 0;
    std::string blocker;
    if (!active_valid || active < 0.4f) {
        blocker = fmt::format("BLOCKED: active {:.2f} < 0.40", active_valid ? active : 0.0f);
    } else if (live.speed_planar_wu >= 2000.0f) {
        blocker = fmt::format("BLOCKED: speed {:.0f} >= 2000", live.speed_planar_wu);
    } else if (live.velocity[2] > 1000.0f) {
        blocker = fmt::format("BLOCKED: vz {:.0f} > 1000", live.velocity[2]);
    } else if (held && !(active >= 2.0f && !endless)) {
        blocker = endless ? "BLOCKED: held, endless upgrade"
                          : fmt::format("BLOCKED: held, active {:.2f} < 2.00", active);
    }
    DrawGateLamp("exit", blocker.empty(), blocker.empty() ? "OPEN" : blocker);
}

void DrawDashLive(const DashSample& live, bool recent_transition) {
    const bool runtime = (live.flags & DashFlagRuntimeFound) != 0;
    const bool aborted = (live.flags & DashFlagMovementUpdateAborted) != 0;
    if (aborted) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                           "MOVEMENT UPDATE ABORTED - this frame's state is last frame's");
    }

    ImGui::BeginDisabled(aborted);
    ImGui::TextUnformatted("Dash");
    ImGui::SameLine(110.0f);
    if (!runtime) {
        ImGui::TextDisabled("no dash runtime live");
    } else {
        const ImVec4 colour = live.dash_state == static_cast<s32>(DashState::Edge)
                                  ? ImVec4(1.0f, 0.4f, 0.3f, 1.0f)
                              : recent_transition ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f)
                                                  : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImGui::TextColored(colour, "%s  -  state %d %s",
                           (live.flags & DashFlagIsNeon) ? "NEON" : "SMOKE", live.dash_state,
                           DashStateName(live.dash_state));
        float in_state = 0.0f;
        TickSeconds(live.tick, live.state_entry_tick, in_state);
        ImGui::SameLine(430.0f);
        ImGui::Text("t_state %.3f s", in_state);

        ImGui::TextUnformatted("Active");
        ImGui::SameLine(110.0f);
        if (float active = 0.0f; TickSeconds(live.tick, live.active_family_tick, active)) {
            ImGui::Text("%.3f s since family latch", active);
        } else {
            ImGui::TextDisabled("-- (family tick not latched)");
        }
        ImGui::SameLine(430.0f);
        float since_activation = 0.0f;
        TickSeconds(live.tick, live.activation_tick, since_activation);
        ImGui::Text("activation %.3f s", since_activation);

        ImGui::TextUnformatted("Progress");
        ImGui::SameLine(110.0f);
        if (std::isnan(live.progress)) {
            ImGui::TextDisabled("--");
        } else {
            // The game's own horizon, max(|total - current| / 1500, 0.25 - activeTime). Printing it
            // next to the observed remaining time is the live self-check of dash-state-spec 0.1.
            float active = 0.0f;
            TickSeconds(live.tick, live.active_family_tick, active);
            const float remaining = std::max(
                live.path_length * (1.0f - live.progress) / SmokeTraverseSpeed, 0.25f - active);
            ImGui::Text("%.3f  (path %.0f wu, mode %d, horizon %.0f ms)", live.progress,
                        live.path_length, live.path_mode, remaining * 1000.0f);
        }
        ImGui::SameLine(430.0f);
        if (live.clip_id == std::numeric_limits<u64>::max()) {
            ImGui::TextDisabled("clip none");
        } else {
            ImGui::Text("clip %#llx%s", static_cast<unsigned long long>(live.clip_id),
                        live.launch_clip != 0 ? "  (launch playing)" : "");
        }
    }

    // wu first: every constant in the game (1500, 1800, 2000, 2250) is in world units, and
    // 1 wu = 1 cm.
    ImGui::TextUnformatted("Speed");
    ImGui::SameLine(110.0f);
    ImGui::Text("%.0f wu/s (%.2f m/s)   planar %.0f wu/s (%.2f m/s)", live.speed_wu,
                live.speed_wu * 0.01f, live.speed_planar_wu, live.speed_planar_wu * 0.01f);
    ImGui::TextUnformatted("Velocity");
    ImGui::SameLine(110.0f);
    ImGui::Text("%+9.1f %+9.1f %+9.1f wu/s%s", live.velocity[0], live.velocity[1], live.velocity[2],
                (live.flags & DashFlagVelocityValid) ? "" : "  (no previous frame)");
    ImGui::TextUnformatted("Requested");
    ImGui::SameLine(110.0f);
    ImGui::Text("%+9.1f %+9.1f %+9.1f wu/s  (entity+0x250)", live.requested_velocity[0],
                live.requested_velocity[1], live.requested_velocity[2]);
    ImGui::TextUnformatted("Position");
    ImGui::SameLine(110.0f);
    ImGui::Text("%+10.1f %+10.1f %+10.1f   yaw %+.3f rad", live.position[0], live.position[1],
                live.position[2], live.facing_yaw);
    ImGui::TextUnformatted("Camera");
    ImGui::SameLine(110.0f);
    ImGui::Text("%+10.1f %+10.1f %+10.1f", live.camera[0], live.camera[1], live.camera[2]);
    ImGui::TextUnformatted("Power");
    ImGui::SameLine(110.0f);
    ImGui::Text("%-9s grounded %-4s n.z %+.3f   stick %+.2f %+.2f   dash button %s",
                PowerName(live.power), (live.flags & DashFlagGrounded) ? "yes" : "no",
                live.ground_normal[2], live.stick_x, live.stick_y,
                (live.flags & DashFlagButtonHeld) ? "held" : "up");

    if (runtime) {
        DrawDashGates(live);
    }
    ImGui::EndDisabled();
}

void DrawDash() {
    if (!ImGui::CollapsingHeader("Dash")) {
        return;
    }

    bool enabled = dash_feed_enabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Sample every guest frame", &enabled)) {
        if (enabled) {
            // Re-latch the upgrade query and drop the finite-difference history: the first sample
            // must not be differenced against wherever the player was when the feed last stopped.
            endless_dash_known.store(false, std::memory_order_relaxed);
            previous_entity.store(0, std::memory_order_relaxed);
        }
        dash_feed_enabled.store(enabled, std::memory_order_relaxed);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear##dash")) {
        for (auto& slot : dash_ring) {
            slot.seq.store(0, std::memory_order_relaxed);
        }
        dash_sequence.store(0, std::memory_order_release);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%llu samples", static_cast<unsigned long long>(
                                            dash_sequence.load(std::memory_order_acquire)));

    std::array<DashSample, DashFeedRows> feed{};
    const std::size_t rows = SnapshotDashSamples(feed.data(), feed.size());
    if (rows == 0) {
        ImGui::TextDisabled("No samples yet - enable the feed once the world has loaded.");
        return;
    }

    // Compare against a few samples back so 3<->4<->5 flicker reads as a flicker instead of
    // averaging into a plausible-looking constant.
    const bool recent_transition =
        feed[0].state_entry_tick != feed[std::min<std::size_t>(rows - 1, 3)].state_entry_tick;
    DrawDashLive(feed[0], recent_transition);

    if (!ImGui::BeginTable("dash_samples", 9,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                           ImVec2(0.0f, 220.0f))) {
        return;
    }
    ImGui::TableSetupColumn("Seq");
    ImGui::TableSetupColumn("dt ms");
    ImGui::TableSetupColumn("State");
    ImGui::TableSetupColumn("t_state");
    ImGui::TableSetupColumn("Speed wu/s");
    ImGui::TableSetupColumn("vz");
    ImGui::TableSetupColumn("Mode");
    ImGui::TableSetupColumn("n.z");
    ImGui::TableSetupColumn("Flags");
    ImGui::TableHeadersRow();

    for (std::size_t i = 0; i < rows; ++i) {
        const auto& entry = feed[i];
        ImGui::PushID(static_cast<int>(entry.seq));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(entry.seq));
        ImGui::TableNextColumn();
        // Newest first, so the delta is measured against the row printed below this one.
        if (i + 1 < rows) {
            ImGui::Text("%+.1f", (static_cast<double>(entry.time_us) -
                                  static_cast<double>(feed[i + 1].time_us)) /
                                     1000.0);
        } else {
            ImGui::TextUnformatted("-");
        }
        ImGui::TableNextColumn();
        if ((entry.flags & DashFlagMovementUpdateAborted) != 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s (stale)",
                               DashStateName(entry.dash_state));
        } else {
            ImGui::TextUnformatted(DashStateName(entry.dash_state));
        }
        ImGui::TableNextColumn();
        if (float in_state = 0.0f; (entry.flags & DashFlagRuntimeFound) &&
                                   TickSeconds(entry.tick, entry.state_entry_tick, in_state)) {
            ImGui::Text("%.3f", in_state);
        } else {
            ImGui::TextUnformatted("-");
        }
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", entry.speed_wu);
        ImGui::TableNextColumn();
        ImGui::Text("%+.0f", entry.velocity[2]);
        ImGui::TableNextColumn();
        ImGui::Text("%d", entry.surface_contact_mode);
        ImGui::TableNextColumn();
        ImGui::Text("%+.3f", entry.contact_normal_z);
        ImGui::TableNextColumn();
        char letters[16] = {};
        DashFlagLetters(entry.flags, letters);
        ImGui::TextUnformatted(letters);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace

bool IsAvailable() {
    return hooks_installed;
}

bool IsOpen() {
    return window_open;
}

void SetOpen(bool open) {
    window_open = open;
}

void InstallHooks(uintptr_t eboot_base, std::string_view game_serial) {
    if (game_serial != "CUSA00004" || hooks_installed) {
        return;
    }
    if (!VerifyMainLoopCall(eboot_base)) {
        return;
    }

    eboot_base_address = eboot_base;
    hooks_installed = InstallMainLoopHook(eboot_base);
    if (hooks_installed) {
        LOG_INFO(Debug, "Installed inFAMOUS Second Son guest main-loop debug executor");
    }
}

void Draw() {
    if (!hooks_installed || !window_open) {
        return;
    }

    if (!ImGui::Begin("inFAMOUS Second Son Debug Menu", &window_open)) {
        ImGui::End();
        return;
    }

    const auto state = action_state.load(std::memory_order_acquire);
    if (state == ActionState::Pending) {
        ImGui::TextDisabled("Action queued for the guest game thread...");
    } else if (state == ActionState::Executing) {
        ImGui::TextDisabled("Action executing on the guest game thread...");
    } else if (const char* outcome = OutcomeText(action_outcome.load(std::memory_order_acquire));
               outcome != nullptr) {
        ImGui::TextUnformatted(outcome);
    }

    DrawPowers();
    DrawWorldLoader();
    DrawDash();
    ImGui::End();
}

} // namespace Core::Devtools::InfamousSecondSonDebugMenu
