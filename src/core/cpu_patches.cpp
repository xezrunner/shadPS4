// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <mutex>
#include <Zydis/Zydis.h>
#include <xbyak/xbyak.h>
#include "common/assert.h"
#include "common/types.h"
#include "core/tls.h"
#include "cpu_patches.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#ifdef __APPLE__
#include <half.hpp>
#include <sys/sysctl.h>
#endif
#endif

using namespace Xbyak::util;

namespace Core {

static Xbyak::Reg ZydisToXbyakRegister(const ZydisRegister reg) {
    if (reg >= ZYDIS_REGISTER_EAX && reg <= ZYDIS_REGISTER_R15D) {
        return Xbyak::Reg32(reg - ZYDIS_REGISTER_EAX + Xbyak::Operand::EAX);
    }
    if (reg >= ZYDIS_REGISTER_RAX && reg <= ZYDIS_REGISTER_R15) {
        return Xbyak::Reg64(reg - ZYDIS_REGISTER_RAX + Xbyak::Operand::RAX);
    }
    if (reg >= ZYDIS_REGISTER_XMM0 && reg <= ZYDIS_REGISTER_XMM31) {
        return Xbyak::Xmm(reg - ZYDIS_REGISTER_XMM0 + xmm0.getIdx());
    }
    if (reg >= ZYDIS_REGISTER_YMM0 && reg <= ZYDIS_REGISTER_YMM31) {
        return Xbyak::Ymm(reg - ZYDIS_REGISTER_YMM0 + ymm0.getIdx());
    }
    UNREACHABLE_MSG("Unsupported register: {}", static_cast<u32>(reg));
}

static Xbyak::Reg ZydisToXbyakRegisterOperand(const ZydisDecodedOperand& operand) {
    ASSERT_MSG(operand.type == ZYDIS_OPERAND_TYPE_REGISTER,
               "Expected register operand, got type: {}", static_cast<u32>(operand.type));

    return ZydisToXbyakRegister(operand.reg.value);
}

static Xbyak::Address ZydisToXbyakMemoryOperand(const ZydisDecodedOperand& operand) {
    ASSERT_MSG(operand.type == ZYDIS_OPERAND_TYPE_MEMORY, "Expected memory operand, got type: {}",
               static_cast<u32>(operand.type));

    if (operand.mem.base == ZYDIS_REGISTER_RIP) {
        return ptr[rip + operand.mem.disp.value];
    }

    Xbyak::RegExp expression{};
    if (operand.mem.base != ZYDIS_REGISTER_NONE) {
        expression = expression + ZydisToXbyakRegister(operand.mem.base);
    }
    if (operand.mem.index != ZYDIS_REGISTER_NONE) {
        if (operand.mem.scale != 0) {
            expression = expression + ZydisToXbyakRegister(operand.mem.index) * operand.mem.scale;
        } else {
            expression = expression + ZydisToXbyakRegister(operand.mem.index);
        }
    }
    if (operand.mem.disp.size != 0 && operand.mem.disp.value != 0) {
        expression = expression + operand.mem.disp.value;
    }

    return ptr[expression];
}

static u64 ZydisToXbyakImmediateOperand(const ZydisDecodedOperand& operand) {
    ASSERT_MSG(operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE,
               "Expected immediate operand, got type: {}", static_cast<u32>(operand.type));
    return operand.imm.value.u;
}

static std::unique_ptr<Xbyak::Operand> ZydisToXbyakOperand(const ZydisDecodedOperand& operand) {
    switch (operand.type) {
    case ZYDIS_OPERAND_TYPE_REGISTER: {
        return std::make_unique<Xbyak::Reg>(ZydisToXbyakRegisterOperand(operand));
    }
    case ZYDIS_OPERAND_TYPE_MEMORY: {
        return std::make_unique<Xbyak::Address>(ZydisToXbyakMemoryOperand(operand));
    }
    default:
        UNREACHABLE_MSG("Unsupported operand type: {}", static_cast<u32>(operand.type));
    }
}

static bool OperandUsesRegister(const Xbyak::Operand* operand, int index) {
    if (operand->isREG()) {
        return operand->getIdx() == index;
    }
    if (operand->isMEM()) {
        const Xbyak::RegExp& reg_exp = operand->getAddress().getRegExp();
        return reg_exp.getBase().getIdx() == index || reg_exp.getIndex().getIdx() == index;
    }
    UNREACHABLE_MSG("Unsupported operand kind: {}", static_cast<u32>(operand->getKind()));
}

static bool IsRegisterAllocated(
    const std::initializer_list<const Xbyak::Operand*>& allocated_registers, const int index) {
    return std::ranges::find_if(allocated_registers.begin(), allocated_registers.end(),
                                [index](const Xbyak::Operand* operand) {
                                    return OperandUsesRegister(operand, index);
                                }) != allocated_registers.end();
}

static Xbyak::Reg AllocateScratchRegister(
    const std::initializer_list<const Xbyak::Operand*> allocated_registers, const u32 bits) {
    for (int index = Xbyak::Operand::R8; index <= Xbyak::Operand::R15; index++) {
        if (!IsRegisterAllocated(allocated_registers, index)) {
            return Xbyak::Reg32e(index, static_cast<int>(bits));
        }
    }
    UNREACHABLE_MSG("Out of scratch registers!");
}

#ifdef __APPLE__

static pthread_key_t stack_pointer_slot;
static pthread_key_t patch_stack_slot;
static std::once_flag patch_context_slots_init_flag;

static_assert(sizeof(void*) == sizeof(u64),
              "Cannot fit a register inside a thread local storage slot.");

static void InitializePatchContextSlots() {
    ASSERT_MSG(pthread_key_create(&stack_pointer_slot, nullptr) == 0,
               "Unable to allocate thread-local register for stack pointer.");
    ASSERT_MSG(pthread_key_create(&patch_stack_slot, nullptr) == 0,
               "Unable to allocate thread-local register for patch stack.");
}

void InitializeThreadPatchStack() {
    std::call_once(patch_context_slots_init_flag, InitializePatchContextSlots);

    const auto* patch_stack = std::malloc(0x1000);
    pthread_setspecific(patch_stack_slot, patch_stack);
}

void CleanupThreadPatchStack() {
    std::call_once(patch_context_slots_init_flag, InitializePatchContextSlots);

    auto* patch_stack = pthread_getspecific(patch_stack_slot);
    if (patch_stack != nullptr) {
        std::free(patch_stack);
        pthread_setspecific(patch_stack_slot, nullptr);
    }
}

/// Saves the stack pointer to thread local storage and loads the patch stack.
static void SaveStack(Xbyak::CodeGenerator& c) {
    std::call_once(patch_context_slots_init_flag, InitializePatchContextSlots);

    // Save stack pointer and load patch stack.
    c.putSeg(gs);
    c.mov(qword[reinterpret_cast<void*>(stack_pointer_slot * sizeof(void*))], rsp);
    c.putSeg(gs);
    c.mov(rsp, qword[reinterpret_cast<void*>(patch_stack_slot * sizeof(void*))]);
}

/// Restores the stack pointer from thread local storage.
static void RestoreStack(Xbyak::CodeGenerator& c) {
    std::call_once(patch_context_slots_init_flag, InitializePatchContextSlots);

    // Save patch stack pointer and load original stack.
    c.putSeg(gs);
    c.mov(qword[reinterpret_cast<void*>(patch_stack_slot * sizeof(void*))], rsp);
    c.putSeg(gs);
    c.mov(rsp, qword[reinterpret_cast<void*>(stack_pointer_slot * sizeof(void*))]);
}

#else

// These utilities are not implemented as we can't save anything to thread local storage without
// temporary registers.
void InitializeThreadPatchStack() {
    // No-op
}

void CleanupThreadPatchStack() {
    // No-op
}

/// Saves the stack pointer to thread local storage and loads the patch stack.
static void SaveStack(Xbyak::CodeGenerator& c) {
    UNIMPLEMENTED();
}

/// Restores the stack pointer from thread local storage.
static void RestoreStack(Xbyak::CodeGenerator& c) {
    UNIMPLEMENTED();
}

#endif

/// Switches to the patch stack, saves registers, and restores the original stack.
static void SaveRegisters(Xbyak::CodeGenerator& c, const std::initializer_list<Xbyak::Reg> regs) {
    SaveStack(c);
    for (const auto& reg : regs) {
        c.push(reg.cvt64());
    }
    RestoreStack(c);
}

/// Switches to the patch stack, restores registers, and restores the original stack.
static void RestoreRegisters(Xbyak::CodeGenerator& c,
                             const std::initializer_list<Xbyak::Reg> regs) {
    SaveStack(c);
    for (const auto& reg : regs) {
        c.pop(reg.cvt64());
    }
    RestoreStack(c);
}

/// Switches to the patch stack and stores all registers.
static void SaveContext(Xbyak::CodeGenerator& c) {
    SaveStack(c);
    for (int reg = Xbyak::Operand::RAX; reg <= Xbyak::Operand::R15; reg++) {
        c.push(Xbyak::Reg64(reg));
    }
    for (int reg = 0; reg <= 7; reg++) {
        c.sub(rsp, 32);
        c.vmovdqu(ptr[rsp], Xbyak::Ymm(reg));
    }
}

/// Restores all registers and restores the original stack.
/// If the destination is a register, it is not restored to preserve the output.
static void RestoreContext(Xbyak::CodeGenerator& c, const Xbyak::Operand& dst) {
    for (int reg = 7; reg >= 0; reg--) {
        if ((!dst.isXMM() && !dst.isYMM()) || dst.getIdx() != reg) {
            c.vmovdqu(Xbyak::Ymm(reg), ptr[rsp]);
        }
        c.add(rsp, 32);
    }
    for (int reg = Xbyak::Operand::R15; reg >= Xbyak::Operand::RAX; reg--) {
        if (!dst.isREG() || dst.getIdx() != reg) {
            c.pop(Xbyak::Reg64(reg));
        } else {
            c.add(rsp, 4);
        }
    }
    RestoreStack(c);
}

#ifdef __APPLE__

static void GenerateANDN(const ZydisDecodedOperand* operands, Xbyak::CodeGenerator& c) {
    const auto dst = ZydisToXbyakRegisterOperand(operands[0]);
    const auto src1 = ZydisToXbyakRegisterOperand(operands[1]);
    const auto src2 = ZydisToXbyakOperand(operands[2]);

    const auto scratch = AllocateScratchRegister({&dst, &src1, src2.get()}, dst.getBit());

    SaveRegisters(c, {scratch});

    c.mov(scratch, src1);
    c.not_(scratch);
    c.and_(scratch, *src2);
    c.mov(dst, scratch);

    RestoreRegisters(c, {scratch});
}

static void GenerateBEXTR(const ZydisDecodedOperand* operands, Xbyak::CodeGenerator& c) {
    const auto dst = ZydisToXbyakRegisterOperand(operands[0]);
    const auto src = ZydisToXbyakOperand(operands[1]);
    const auto start_len = ZydisToXbyakRegisterOperand(operands[2]);

    const Xbyak::Reg32e shift(Xbyak::Operand::RCX, static_cast<int>(start_len.getBit()));
    const auto scratch1 =
        AllocateScratchRegister({&dst, src.get(), &start_len, &shift}, dst.getBit());
    const auto scratch2 =
        AllocateScratchRegister({&dst, src.get(), &start_len, &shift, &scratch1}, dst.getBit());

    if (dst.getIdx() == shift.getIdx()) {
        SaveRegisters(c, {scratch1, scratch2});
    } else {
        SaveRegisters(c, {scratch1, scratch2, shift});
    }

    c.mov(scratch1, *src);
    if (shift.getIdx() != start_len.getIdx()) {
        c.mov(shift, start_len);
    }

    c.shr(scratch1, shift.cvt8());
    c.shr(shift, 8);
    c.mov(scratch2, 1);
    c.shl(scratch2, shift.cvt8());
    c.dec(scratch2);

    c.mov(dst, scratch1);
    c.and_(dst, scratch2);

    if (dst.getIdx() == shift.getIdx()) {
        RestoreRegisters(c, {scratch2, scratch1});
    } else {
        RestoreRegisters(c, {shift, scratch2, scratch1});
    }
}

static void GenerateBLSI(const ZydisDecodedOperand* operands, Xbyak::CodeGenerator& c) {
    const auto dst = ZydisToXbyakRegisterOperand(operands[0]);
    const auto src = ZydisToXbyakOperand(operands[1]);

    const auto scratch = AllocateScratchRegister({&dst, src.get()}, dst.getBit());

    SaveRegisters(c, {scratch});

    c.mov(scratch, *src);
    c.neg(scratch);
    c.and_(scratch, *src);
    c.mov(dst, scratch);

    RestoreRegisters(c, {scratch});
}

static void GenerateBLSMSK(const ZydisDecodedOperand* operands, Xbyak::CodeGenerator& c) {
    const auto dst = ZydisToXbyakRegisterOperand(operands[0]);
    const auto src = ZydisToXbyakOperand(operands[1]);

    const auto scratch = AllocateScratchRegister({&dst, src.get()}, dst.getBit());

    SaveRegisters(c, {scratch});

    c.mov(scratch, *src);
    c.dec(scratch);
    c.xor_(scratch, *src);
    c.mov(dst, scratch);

    RestoreRegisters(c, {scratch});
}

static void GenerateBLSR(const ZydisDecodedOperand* operands, Xbyak::CodeGenerator& c) {
    const auto dst = ZydisToXbyakRegisterOperand(operands[0]);
    const auto src = ZydisToXbyakOperand(operands[1]);

    const auto scratch = AllocateScratchRegister({&dst, src.get()}, dst.getBit());

    SaveRegisters(c, {scratch});

    c.mov(scratch, *src);
    c.dec(scratch);
    c.and_(scratch, *src);
    c.mov(dst, scratch);

    RestoreRegisters(c, {scratch});
}

static __attribute__((sysv_abi)) void PerformVCVTPH2PS(float* out, const half_float::half* in,
                                                       const u32 count) {
    for (u32 i = 0; i < count; i++) {
        out[i] = half_float::half_cast<float>(in[i]);
    }
}

static void GenerateVCVTPH2PS(const ZydisDecodedOperand* operands, Xbyak::CodeGenerator& c) {
    const auto dst = ZydisToXbyakRegisterOperand(operands[0]);
    const auto src = ZydisToXbyakOperand(operands[1]);

    const auto float_count = dst.getBit() / 32;
    const auto byte_count = float_count * 4;

    SaveContext(c);

    // Allocate stack space for outputs and load into first parameter.
    c.sub(rsp, byte_count);
    c.mov(rdi, rsp);

    if (src->isXMM()) {
        // Allocate stack space for inputs and load into second parameter.
        c.sub(rsp, byte_count);
        c.mov(rsi, rsp);

        // Move input to the allocated space.
        c.movdqu(ptr[rsp], *reinterpret_cast<Xbyak::Xmm*>(src.get()));
    } else {
        c.lea(rsi, src->getAddress());
    }

    // Load float count into third parameter.
    c.mov(rdx, float_count);

    c.mov(rax, reinterpret_cast<u64>(PerformVCVTPH2PS));
    c.call(rax);

    if (src->isXMM()) {
        // Clean up after inputs space.
        c.add(rsp, byte_count);
    }

    // Load outputs into destination register and clean up space.
    if (dst.isYMM()) {
        c.vmovdqu(*reinterpret_cast<const Xbyak::Ymm*>(&dst), ptr[rsp]);
    } else {
        c.movdqu(*reinterpret_cast<const Xbyak::Xmm*>(&dst), ptr[rsp]);
    }
    c.add(rsp, byte_count);

    RestoreContext(c, dst);
}

using SingleToHalfFloatConverter = half_float::half (*)(float);
static const SingleToHalfFloatConverter SingleToHalfFloatConverters[4] = {
    half_float::half_cast<half_float::half, std::round_to_nearest, float>,
    half_float::half_cast<half_float::half, std::round_toward_neg_infinity, float>,
    half_float::half_cast<half_float::half, std::round_toward_infinity, float>,
    half_float::half_cast<half_float::half, std::round_toward_zero, float>,
};

static __attribute__((sysv_abi)) void PerformVCVTPS2PH(half_float::half* out, const float* in,
                                                       const u32 count, const u8 rounding_mode) {
    const auto conversion_func = SingleToHalfFloatConverters[rounding_mode];

    for (u32 i = 0; i < count; i++) {
        out[i] = conversion_func(in[i]);
    }
}

static void GenerateVCVTPS2PH(const ZydisDecodedOperand* operands, Xbyak::CodeGenerator& c) {
    const auto dst = ZydisToXbyakOperand(operands[0]);
    const auto src = ZydisToXbyakRegisterOperand(operands[1]);
    const auto ctrl = ZydisToXbyakImmediateOperand(operands[2]);

    const auto float_count = src.getBit() / 32;
    const auto byte_count = float_count * 4;

    SaveContext(c);

    if (dst->isXMM()) {
        // Allocate stack space for outputs and load into first parameter.
        c.sub(rsp, byte_count);
        c.mov(rdi, rsp);
    } else {
        c.lea(rdi, dst->getAddress());
    }

    // Allocate stack space for inputs and load into second parameter.
    c.sub(rsp, byte_count);
    c.mov(rsi, rsp);

    // Move input to the allocated space.
    if (src.isYMM()) {
        c.vmovdqu(ptr[rsp], *reinterpret_cast<const Xbyak::Ymm*>(&src));
    } else {
        c.movdqu(ptr[rsp], *reinterpret_cast<const Xbyak::Xmm*>(&src));
    }

    // Load float count into third parameter.
    c.mov(rdx, float_count);

    // Load rounding mode into fourth parameter.
    if (ctrl & 4) {
        // Load from MXCSR.RC.
        c.stmxcsr(ptr[rsp - 4]);
        c.mov(rcx, ptr[rsp - 4]);
        c.shr(rcx, 13);
        c.and_(rcx, 3);
    } else {
        c.mov(rcx, ctrl & 3);
    }

    c.mov(rax, reinterpret_cast<u64>(PerformVCVTPS2PH));
    c.call(rax);

    // Clean up after inputs space.
    c.add(rsp, byte_count);

    if (dst->isXMM()) {
        // Load outputs into destination register and clean up space.
        c.movdqu(*reinterpret_cast<Xbyak::Xmm*>(dst.get()), ptr[rsp]);
        c.add(rsp, byte_count);
    }

    RestoreContext(c, *dst);
}

static bool FilterRosetta2Only(const ZydisDecodedOperand*) {
    int ret = 0;
    size_t size = sizeof(ret);
    if (sysctlbyname("sysctl.proc_translated", &ret, &size, nullptr, 0) != 0) {
        return false;
    }
    return ret;
}

#endif // __APPLE__

static bool FilterTcbAccess(const ZydisDecodedOperand* operands) {
    const auto& dst_op = operands[0];
    const auto& src_op = operands[1];

    // Patch only 'mov (64-bit register), fs:[0]'
    return src_op.type == ZYDIS_OPERAND_TYPE_MEMORY && src_op.mem.segment == ZYDIS_REGISTER_FS &&
           src_op.mem.base == ZYDIS_REGISTER_NONE && src_op.mem.index == ZYDIS_REGISTER_NONE &&
           src_op.mem.disp.value == 0 && dst_op.reg.value >= ZYDIS_REGISTER_RAX &&
           dst_op.reg.value <= ZYDIS_REGISTER_R15;
}

static void GenerateTcbAccess(const ZydisDecodedOperand* operands, Xbyak::CodeGenerator& c) {
    const auto dst = ZydisToXbyakRegisterOperand(operands[0]);
    const auto slot = GetTcbKey();

#if defined(_WIN32)
    // The following logic is based on the Kernel32.dll asm of TlsGetValue
    static constexpr u32 TlsSlotsOffset = 0x1480;
    static constexpr u32 TlsExpansionSlotsOffset = 0x1780;
    static constexpr u32 TlsMinimumAvailable = 64;

    // Load the pointer to the table of TLS slots.
    c.putSeg(gs);
    if (slot < TlsMinimumAvailable) {
        // Load the pointer to TLS slots.
        c.mov(dst, ptr[reinterpret_cast<void*>(TlsSlotsOffset + slot * sizeof(LPVOID))]);
    } else {
        const u32 tls_index = slot - TlsMinimumAvailable;

        // Load the pointer to the table of TLS expansion slots.
        c.mov(dst, ptr[reinterpret_cast<void*>(TlsExpansionSlotsOffset)]);
        // Load the pointer to our buffer.
        c.mov(dst, qword[dst + tls_index * sizeof(LPVOID)]);
    }
#elif defined(__APPLE__)
    // The following logic is based on the Darwin implementation of _os_tsd_get_direct, used by
    // pthread_getspecific https://github.com/apple/darwin-xnu/blob/main/libsyscall/os/tsd.h#L89-L96
    c.putSeg(gs);
    c.mov(dst, qword[reinterpret_cast<void*>(slot * sizeof(void*))]);
#else
    const auto src = ZydisToXbyakMemoryOperand(operands[1]);

    // Replace fs read with gs read.
    c.putSeg(gs);
    c.mov(dst, src);
#endif
}

using PatchFilter = bool (*)(const ZydisDecodedOperand*);
using InstructionGenerator = void (*)(const ZydisDecodedOperand*, Xbyak::CodeGenerator&);
struct PatchInfo {
    /// Filter for more granular patch conditions past just the instruction mnemonic.
    PatchFilter filter;

    /// Generator for the patch/trampoline.
    InstructionGenerator generator;

    /// Whether to use a trampoline for this patch.
    bool trampoline;
};

static const std::unordered_map<ZydisMnemonic, PatchInfo> Patches = {
#if defined(_WIN32) || defined(__APPLE__)
    // Windows and Apple need a trampoline.
    {ZYDIS_MNEMONIC_MOV, {FilterTcbAccess, GenerateTcbAccess, true}},
#else
    {ZYDIS_MNEMONIC_MOV, {FilterTcbAccess, GenerateTcbAccess, false}},
#endif

#ifdef __APPLE__
    // Patches for instruction sets not supported by Rosetta 2.
    // BMI1
    {ZYDIS_MNEMONIC_ANDN, {FilterRosetta2Only, GenerateANDN, true}},
    {ZYDIS_MNEMONIC_BEXTR, {FilterRosetta2Only, GenerateBEXTR, true}},
    {ZYDIS_MNEMONIC_BLSI, {FilterRosetta2Only, GenerateBLSI, true}},
    {ZYDIS_MNEMONIC_BLSMSK, {FilterRosetta2Only, GenerateBLSMSK, true}},
    {ZYDIS_MNEMONIC_BLSR, {FilterRosetta2Only, GenerateBLSR, true}},
    // F16C
    {ZYDIS_MNEMONIC_VCVTPH2PS, {FilterRosetta2Only, GenerateVCVTPH2PS, true}},
    {ZYDIS_MNEMONIC_VCVTPS2PH, {FilterRosetta2Only, GenerateVCVTPS2PH, true}},
#endif
};

void PatchInstructions(u64 segment_addr, u64 segment_size, Xbyak::CodeGenerator& c) {
    if (Patches.empty()) {
        // Nothing to patch on this platform.
        return;
    }

    ZydisDecoder instr_decoder;
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    ZydisDecoderInit(&instr_decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    u8* code = reinterpret_cast<u8*>(segment_addr);
    u8* end = code + segment_size;
    while (code < end) {
        ZyanStatus status =
            ZydisDecoderDecodeFull(&instr_decoder, code, end - code, &instruction, operands);
        if (!ZYAN_SUCCESS(status)) {
            code++;
            continue;
        }

        if (Patches.contains(instruction.mnemonic)) {
            auto patch_info = Patches.at(instruction.mnemonic);
            if (patch_info.filter(operands)) {
                auto patch_gen = Xbyak::CodeGenerator(instruction.length, code);

                if (patch_info.trampoline) {
                    const auto trampoline_ptr = c.getCurr();

                    patch_info.generator(operands, c);

                    // Return to the following instruction at the end of the trampoline.
                    c.jmp(code + instruction.length);

                    // Replace instruction with near jump to the trampoline.
                    patch_gen.jmp(trampoline_ptr, Xbyak::CodeGenerator::LabelType::T_NEAR);
                } else {
                    patch_info.generator(operands, patch_gen);
                }

                const auto patch_size = patch_gen.getCurr() - code;
                if (patch_size > 0) {
                    ASSERT_MSG(instruction.length >= patch_size,
                               "Instruction {} with length {} is too short to replace at: {}",
                               ZydisMnemonicGetString(instruction.mnemonic), instruction.length,
                               fmt::ptr(code));

                    // Fill remaining space with nops.
                    patch_gen.nop(instruction.length - patch_size);

                    LOG_DEBUG(Core, "Patched instruction '{}' at: {}",
                              ZydisMnemonicGetString(instruction.mnemonic), fmt::ptr(code));
                }
            }
        }

        code += instruction.length;
    }
}

} // namespace Core
