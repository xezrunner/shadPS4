// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/alignment.h"
#include "common/assert.h"
#include "common/config.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/string_util.h"
#include "common/thread.h"
#include "core/aerolib/aerolib.h"
#include "core/aerolib/stubs.h"
#include "core/libraries/kernel/memory_management.h"
#include "core/libraries/kernel/thread_management.h"
#include "core/linker.h"
#include "core/memory.h"
#include "core/tls.h"
#include "core/virtual_memory.h"

#include "Zydis/Zydis.h"
#include "xbyak/xbyak.h"

#include <set>
#include <cinttypes>


namespace Core {

using ExitFunc = PS4_SYSV_ABI void (*)();

static PS4_SYSV_ABI void ProgramExitFunc() {
    fmt::print("exit function called\n");
}


void GenerateTrampoline(u64 hle_handler, u64 context_base);

__declspec(align(32)) struct Context {
    u64 gpr[16];
    u64 ymm[16 * 4];
    u64 rip;
    u64 rflags;

    // misc
    u64 host_rsp;
    u64 trampoline_ret;
};
Context thread_context;

auto gen = new Xbyak::CodeGenerator(64 * 1024 * 1024);

constexpr auto rip_offs = offsetof(Context, rip);
constexpr auto ymm_offs = offsetof(Context, ymm[0]);
constexpr auto ymm_size = 32;
constexpr auto rflags_offs = offsetof(Context, rflags);
constexpr auto host_rsp_offs = offsetof(Context, host_rsp);
constexpr auto trampoline_ret_offs = offsetof(Context, trampoline_ret);

std::unordered_map<u64, PS4_SYSV_ABI u64 (*)()> translated_entries;

void push_abi_regs() {
    using namespace Xbyak;
    for (int i = 3; i < 16; i++) {
        if (i == 4)
            continue;
        gen->push(Reg64(i));
    }
}

void pop_abi_regs() {
    using namespace Xbyak;
    for (int i = 15; i >= 3; i--) {
        if (i == 4)
            continue;
        gen->pop(Reg64(i));
    }
}
auto TranslateCode(u8* runtime_address, u64 context_base) -> PS4_SYSV_ABI u64 (*)() {
    printf("TranslateCode: %p, ctx: %llX\n", runtime_address, context_base);

    using namespace Xbyak;
    using namespace Xbyak::util;

    auto Entry = (PS4_SYSV_ABI u64(*)())gen->getCurr();

    const Reg* reg_z2x[ZYDIS_REGISTER_MAX_VALUE] = {
        [ZYDIS_REGISTER_AL] = &gen->rax,      [ZYDIS_REGISTER_CL] = &gen->rcx,
        [ZYDIS_REGISTER_DL] = &gen->rdx,      [ZYDIS_REGISTER_BL] = &gen->rbx,
        [ZYDIS_REGISTER_AH] = &gen->rax,      [ZYDIS_REGISTER_CH] = &gen->rcx,
        [ZYDIS_REGISTER_DH] = &gen->rdx,      [ZYDIS_REGISTER_BH] = &gen->rbx,
        [ZYDIS_REGISTER_SPL] = &gen->rsp,     [ZYDIS_REGISTER_BPL] = &gen->rbp,
        [ZYDIS_REGISTER_SIL] = &gen->rsi,     [ZYDIS_REGISTER_DIL] = &gen->rdi,
        [ZYDIS_REGISTER_R8B] = &gen->r8,      [ZYDIS_REGISTER_R9B] = &gen->r9,
        [ZYDIS_REGISTER_R10B] = &gen->r10,    [ZYDIS_REGISTER_R11B] = &gen->r11,
        [ZYDIS_REGISTER_R12B] = &gen->r12,    [ZYDIS_REGISTER_R13B] = &gen->r13,
        [ZYDIS_REGISTER_R14B] = &gen->r14,    [ZYDIS_REGISTER_R15B] = &gen->r15,

        [ZYDIS_REGISTER_AX] = &gen->rax,      [ZYDIS_REGISTER_CX] = &gen->rcx,
        [ZYDIS_REGISTER_DX] = &gen->rdx,      [ZYDIS_REGISTER_BX] = &gen->rbx,
        [ZYDIS_REGISTER_SP] = &gen->rsp,      [ZYDIS_REGISTER_BP] = &gen->rbp,
        [ZYDIS_REGISTER_SI] = &gen->rsi,      [ZYDIS_REGISTER_DI] = &gen->rdi,
        [ZYDIS_REGISTER_R8W] = &gen->r8,      [ZYDIS_REGISTER_R9W] = &gen->r9,
        [ZYDIS_REGISTER_R10W] = &gen->r10,    [ZYDIS_REGISTER_R11W] = &gen->r11,
        [ZYDIS_REGISTER_R12W] = &gen->r12,    [ZYDIS_REGISTER_R13W] = &gen->r13,
        [ZYDIS_REGISTER_R14W] = &gen->r14,    [ZYDIS_REGISTER_R15W] = &gen->r15,

        [ZYDIS_REGISTER_EAX] = &gen->rax,     [ZYDIS_REGISTER_ECX] = &gen->rcx,
        [ZYDIS_REGISTER_EDX] = &gen->rdx,     [ZYDIS_REGISTER_EBX] = &gen->rbx,
        [ZYDIS_REGISTER_ESP] = &gen->rsp,     [ZYDIS_REGISTER_EBP] = &gen->rbp,
        [ZYDIS_REGISTER_ESI] = &gen->rsi,     [ZYDIS_REGISTER_EDI] = &gen->rdi,
        [ZYDIS_REGISTER_R8D] = &gen->r8,      [ZYDIS_REGISTER_R9D] = &gen->r9,
        [ZYDIS_REGISTER_R10D] = &gen->r10,    [ZYDIS_REGISTER_R11D] = &gen->r11,
        [ZYDIS_REGISTER_R12D] = &gen->r12,    [ZYDIS_REGISTER_R13D] = &gen->r13,
        [ZYDIS_REGISTER_R14D] = &gen->r14,    [ZYDIS_REGISTER_R15D] = &gen->r15,

        [ZYDIS_REGISTER_RAX] = &gen->rax,     [ZYDIS_REGISTER_RCX] = &gen->rcx,
        [ZYDIS_REGISTER_RDX] = &gen->rdx,     [ZYDIS_REGISTER_RBX] = &gen->rbx,
        [ZYDIS_REGISTER_RSP] = &gen->rsp,     [ZYDIS_REGISTER_RBP] = &gen->rbp,
        [ZYDIS_REGISTER_RSI] = &gen->rsi,     [ZYDIS_REGISTER_RDI] = &gen->rdi,
        [ZYDIS_REGISTER_R8] = &gen->r8,       [ZYDIS_REGISTER_R9] = &gen->r9,
        [ZYDIS_REGISTER_R10] = &gen->r10,     [ZYDIS_REGISTER_R11] = &gen->r11,
        [ZYDIS_REGISTER_R12] = &gen->r12,     [ZYDIS_REGISTER_R13] = &gen->r13,
        [ZYDIS_REGISTER_R14] = &gen->r14,     [ZYDIS_REGISTER_R15] = &gen->r15,

        [ZYDIS_REGISTER_XMM0] = &gen->ymm0,   [ZYDIS_REGISTER_XMM1] = &gen->ymm1,
        [ZYDIS_REGISTER_XMM2] = &gen->ymm2,   [ZYDIS_REGISTER_XMM3] = &gen->ymm3,
        [ZYDIS_REGISTER_XMM4] = &gen->ymm4,   [ZYDIS_REGISTER_XMM5] = &gen->ymm5,
        [ZYDIS_REGISTER_XMM6] = &gen->ymm6,   [ZYDIS_REGISTER_XMM7] = &gen->ymm7,
        [ZYDIS_REGISTER_XMM8] = &gen->ymm8,   [ZYDIS_REGISTER_XMM9] = &gen->ymm9,
        [ZYDIS_REGISTER_XMM10] = &gen->ymm10, [ZYDIS_REGISTER_XMM11] = &gen->ymm11,
        [ZYDIS_REGISTER_XMM12] = &gen->ymm12, [ZYDIS_REGISTER_XMM13] = &gen->ymm13,
        [ZYDIS_REGISTER_XMM14] = &gen->ymm14, [ZYDIS_REGISTER_XMM15] = &gen->ymm15,

        [ZYDIS_REGISTER_YMM0] = &gen->ymm0,   [ZYDIS_REGISTER_YMM1] = &gen->ymm1,
        [ZYDIS_REGISTER_YMM2] = &gen->ymm2,   [ZYDIS_REGISTER_YMM3] = &gen->ymm3,
        [ZYDIS_REGISTER_YMM4] = &gen->ymm4,   [ZYDIS_REGISTER_YMM5] = &gen->ymm5,
        [ZYDIS_REGISTER_YMM6] = &gen->ymm6,   [ZYDIS_REGISTER_YMM7] = &gen->ymm7,
        [ZYDIS_REGISTER_YMM8] = &gen->ymm8,   [ZYDIS_REGISTER_YMM9] = &gen->ymm9,
        [ZYDIS_REGISTER_YMM10] = &gen->ymm10, [ZYDIS_REGISTER_YMM11] = &gen->ymm11,
        [ZYDIS_REGISTER_YMM12] = &gen->ymm12, [ZYDIS_REGISTER_YMM13] = &gen->ymm13,
        [ZYDIS_REGISTER_YMM14] = &gen->ymm14, [ZYDIS_REGISTER_YMM15] = &gen->ymm15,
    };

    push_abi_regs();

    gen->mov(gen->rax, context_base + host_rsp_offs);
    gen->mov(gen->qword[gen->rax], gen->rsp);

    std::set<ZydisRegister> UsedRegs;

    ZydisDisassembledInstruction instruction;
    while (ZYAN_SUCCESS(ZydisDisassembleIntel(
        /* machine_mode:    */ ZYDIS_MACHINE_MODE_LONG_64,
        /* runtime_address: */ (uint64_t)runtime_address,
        /* buffer:          */ runtime_address,
        /* length:          */ 15,
        /* instruction:     */ &instruction))) {
        printf("%016" PRIX64 "  %s\n", (u64)runtime_address, instruction.text);

        auto next_address = runtime_address + instruction.info.length;

        UsedRegs.clear();

        bool UsesFlags = false;

        if (instruction.info.meta.branch_type != ZYDIS_BRANCH_TYPE_NONE) {
// Let's assume that patch TLS works
//            assert((instruction.info.attributes &
//                    (ZYDIS_ATTRIB_HAS_SEGMENT_FS | ZYDIS_ATTRIB_HAS_SEGMENT_GS)) == 0);
            if (instruction.info.mnemonic == ZYDIS_MNEMONIC_CALL ||
                instruction.info.mnemonic == ZYDIS_MNEMONIC_JMP) {

                if (instruction.info.mnemonic == ZYDIS_MNEMONIC_CALL) {
                    gen->mov(gen->rax, context_base + 4 * 8);
                    gen->mov(gen->rcx, gen->rax);
                    gen->mov(gen->rsp, gen->qword[gen->rax]);
                    gen->mov(gen->rax, (u64)next_address);
                    gen->push(gen->rax);
                    gen->mov(gen->qword[gen->rcx], gen->rsp);
                }

                gen->mov(gen->rax, context_base);
                gen->mov(gen->rsp, gen->qword[gen->rax + host_rsp_offs]);
                pop_abi_regs();

                if (instruction.operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
                    gen->mov(gen->rax, (u64)next_address + instruction.operands[0].imm.value.s);
                } else if (instruction.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER) {
                    gen->mov(
                        gen->rax,
                        ptr[gen->rax + reg_z2x[instruction.operands[0].reg.value]->getIdx() * 8]);
                } else if (instruction.operands[0].type == ZYDIS_OPERAND_TYPE_MEMORY) {
                    if (instruction.operands[0].mem.base == ZYDIS_REGISTER_RIP) {
                        assert(instruction.operands[0].mem.index == ZYDIS_REGISTER_NONE);
                        // assert(instruction.operands[0].mem.disp.has_displacement);
                        gen->mov(gen->rax,
                                    (u64)next_address + instruction.operands[0].mem.disp.value);
                        gen->mov(rax, ptr[rax]);
                    } else {

                        gen->mov(gen->rax, context_base);

                        if (instruction.operands[0].mem.index == ZYDIS_REGISTER_NONE) {
                            gen->mov(
                                rcx,
                                ptr[rax + reg_z2x[instruction.operands[0].mem.base]->getIdx() * 8]);
                        } else {
                            gen->mov(
                                rcx,
                                ptr[rax + reg_z2x[instruction.operands[0].mem.index]->getIdx() * 8]);
                            if (instruction.operands[0].mem.scale != 1) {
                                gen->lea(rcx, ptr[rcx * instruction.operands[0].mem.scale]);
                            }
                            gen->add(
                                rcx,
                                ptr[rax + reg_z2x[instruction.operands[0].mem.base]->getIdx() * 8]);
                        }
                        
                        if (instruction.operands[0].mem.disp.value) {
                            gen->mov(rax, ptr[rcx + instruction.operands[0].mem.disp.value]);
                        } else {
                            gen->mov(rax, ptr[rcx]);
                        }
                    }
                } else {
                    assert(false);
                }
                gen->ret();
            } else if (instruction.info.mnemonic == ZYDIS_MNEMONIC_RET) {
                gen->mov(gen->rax, context_base + 4 * 8);
                gen->mov(gen->rsp, gen->qword[gen->rax]);
                gen->pop(gen->rcx);
                gen->mov(gen->qword[gen->rax], gen->rsp);

                gen->mov(gen->rax, context_base);
                gen->mov(gen->rsp, gen->qword[gen->rax + host_rsp_offs]);
                pop_abi_regs();

                gen->mov(gen->rax, gen->rcx);
                gen->ret();
            } else if (instruction.info.mnemonic == ZYDIS_MNEMONIC_JB ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JBE ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JL ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JLE ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JNB ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JNBE ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JNL ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JNLE ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JNO ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JNP ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JNS ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JNZ ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JO ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JP ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JS ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_JZ) {
                void (CodeGenerator::*jump_map[])(const Label& label,
                                                    CodeGenerator::LabelType type) = {
                    [ZYDIS_MNEMONIC_JB] = &CodeGenerator::jb,
                    [ZYDIS_MNEMONIC_JBE] = &CodeGenerator::jbe,
                    [ZYDIS_MNEMONIC_JL] = &CodeGenerator::jl,
                    [ZYDIS_MNEMONIC_JLE] = &CodeGenerator::jle,
                    [ZYDIS_MNEMONIC_JNB] = &CodeGenerator::jnb,
                    [ZYDIS_MNEMONIC_JNBE] = &CodeGenerator::jnbe,
                    [ZYDIS_MNEMONIC_JNL] = &CodeGenerator::jnl,
                    [ZYDIS_MNEMONIC_JNLE] = &CodeGenerator::jnle,
                    [ZYDIS_MNEMONIC_JNO] = &CodeGenerator::jno,
                    [ZYDIS_MNEMONIC_JNP] = &CodeGenerator::jnp,
                    [ZYDIS_MNEMONIC_JNS] = &CodeGenerator::jns,
                    [ZYDIS_MNEMONIC_JNZ] = &CodeGenerator::jnz,
                    [ZYDIS_MNEMONIC_JO] = &CodeGenerator::jo,
                    [ZYDIS_MNEMONIC_JP] = &CodeGenerator::jp,
                    [ZYDIS_MNEMONIC_JS] = &CodeGenerator::js,
                    [ZYDIS_MNEMONIC_JZ] = &CodeGenerator::jz,
                };

                assert(instruction.operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE);

                // restore regs, rsp
                gen->mov(gen->rax, context_base + host_rsp_offs);
                gen->mov(gen->rsp, gen->qword[gen->rax]);
                pop_abi_regs();

                // load flags
                gen->mov(gen->rax, context_base);
                gen->mov(rax, ptr[gen->rax + rflags_offs]);
                gen->push(rax);
                gen->popf();

                u64 dest_addr = (u64)next_address + instruction.operands[0].imm.value.s;

                gen->mov(rax, (u64)dest_addr); // if jump is taken, go to dest_addr
                Label dest_jump;
                (gen->*jump_map[instruction.info.mnemonic])(dest_jump, CodeGenerator::T_NEAR);
                gen->mov(rax, (u64)next_address); // else fall through to next_addr
                gen->L(dest_jump);

                // all done, return
                gen->ret();

            } else if (instruction.info.mnemonic == ZYDIS_MNEMONIC_LOOP ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_LOOPNE ||
                        instruction.info.mnemonic == ZYDIS_MNEMONIC_LOOPE) {
                void (CodeGenerator::*jump_map[])(const Label& label) = {
                    [ZYDIS_MNEMONIC_LOOP] = &CodeGenerator::loop,
                    [ZYDIS_MNEMONIC_LOOPNE] = &CodeGenerator::loopne,
                    [ZYDIS_MNEMONIC_LOOPE] = &CodeGenerator::loope};

                // restore regs, rsp
                gen->mov(gen->rax, context_base + host_rsp_offs);
                gen->mov(gen->rsp, gen->qword[gen->rax]);
                pop_abi_regs();

                // load flags
                gen->mov(gen->rax, context_base);
                // backup ctx
                gen->mov(rdx, rax);

                gen->mov(rax, ptr[gen->rax + rflags_offs]);
                gen->push(rax);
                gen->popf();

                // load rcx
                gen->mov(rcx, ptr[rdx + rcx.getIdx() * 8]);

                u64 dest_addr = (u64)next_address + instruction.operands[0].imm.value.s;

                gen->mov(rax, (u64)dest_addr); // if jump is taken, go to dest_addr
                Label dest_jump;
                (gen->*jump_map[instruction.info.mnemonic])(dest_jump);
                gen->mov(rax, (u64)next_address); // else fall through to next_addr
                gen->L(dest_jump);

                // store rcx

                gen->mov(ptr[gen->rdx + rcx.getIdx() * 8], rcx);

                gen->ret();

            } else {
                // Handle more Branches
                assert(false);
            }
            break;
        } else {
            // Let's assume that patch TLS works
            //assert((instruction.info.attributes &
            //        (ZYDIS_ATTRIB_HAS_SEGMENT_FS | ZYDIS_ATTRIB_HAS_SEGMENT_GS)) == 0);

            for (int i = 0; i < instruction.info.operand_count; i++) {
                auto operand = &instruction.operands[i];
                if (operand->type == ZYDIS_OPERAND_TYPE_REGISTER) {
                    if (operand->reg.value == ZYDIS_REGISTER_RFLAGS) {
                        UsesFlags = true;
                    } else {
                        UsedRegs.insert(operand->reg.value);
                    }
                } else if (operand->type == ZYDIS_OPERAND_TYPE_MEMORY) {
                    if (operand->mem.base && operand->mem.base != ZYDIS_REGISTER_RIP)
                        UsedRegs.insert(operand->mem.base);
                    if (operand->mem.index)
                        UsedRegs.insert(operand->mem.index);
                }
            }

            if (UsesFlags) {
                gen->mov(gen->rax, context_base);
                gen->mov(rsp, ptr[gen->rax + host_rsp_offs]);
                gen->mov(rax, ptr[gen->rax + rflags_offs]);
                gen->push(rax);
                gen->popf();
            }

            u16 used_mask = 0;

            for (auto used_reg : UsedRegs) {
                if (reg_z2x[used_reg] && reg_z2x[used_reg]->isREG()) {
                    used_mask |= 1 << reg_z2x[used_reg]->getIdx();
                }
            }

            u16 unused_mask = ~used_mask;

            // not really possible?
            assert(unused_mask != 0);

            Reg64 context_reg;
            Reg64 rip_reg;

            int free_reg = 0;

            for (; free_reg < 16; free_reg++) {
                if (unused_mask & (1 << free_reg)) {
                    context_reg = Reg64(free_reg);
                    break;
                }
            }

            free_reg++;

            for (; free_reg < 16; free_reg++) {
                if (unused_mask & (1 << free_reg)) {
                    rip_reg = Reg64(free_reg);
                    break;
                }
            }

            if (instruction.info.attributes & ZYDIS_ATTRIB_IS_RELATIVE) {
                gen->mov(gen->rax, (u64)next_address);
                gen->mov(rip_reg, gen->rax);
            }

            gen->mov(gen->rax, context_base);

            if (context_reg != gen->rax) {
                gen->mov(context_reg, gen->rax);
            }

            for (auto used_reg : UsedRegs) {
                if (!reg_z2x[used_reg]) {
                    assert(false);
                    // if (used_reg == ZYDIS_REGISTER_RIP) {
                    //	// handled
                    ////} else if (used_reg == ZYDIS_REGISTER_EFLAGS) {
                    //} else {
                    //}
                } else if (reg_z2x[used_reg]->isREG()) {
                    gen->mov(*reg_z2x[used_reg],
                                gen->ptr[context_reg + reg_z2x[used_reg]->getIdx() * 8]);
                } else if (reg_z2x[used_reg]->isYMM()) {
                    gen->vmovaps(
                        *(Ymm*)reg_z2x[used_reg],
                        gen->ptr[context_reg + ymm_offs + reg_z2x[used_reg]->getIdx() * ymm_size]);
                } else {
                    assert(false);
                }
            }

            if (instruction.info.attributes & ZYDIS_ATTRIB_IS_RELATIVE) {
                assert(rip_reg.getIdx() < 8);
                assert(instruction.info.raw.modrm.offset != 0);
                for (int op_byte = 0; op_byte < instruction.info.length; op_byte++) {
                    if (op_byte == instruction.info.raw.modrm.offset) {
                        assert(instruction.info.raw.modrm.mod == 0 &&
                                instruction.info.raw.modrm.rm == 5);
                        gen->db((0x2 << 6) | (instruction.info.raw.modrm.reg << 3) |
                                (rip_reg.getIdx() & 7));
                    } else {
                        gen->db(runtime_address[op_byte]);
                    }
                }
            } else {
                for (int op_byte = 0; op_byte < instruction.info.length; op_byte++)
                    gen->db(runtime_address[op_byte]);
            }

            for (auto used_reg : UsedRegs) {
                if (reg_z2x[used_reg]->isREG()) {
                    gen->mov(gen->ptr[context_reg + reg_z2x[used_reg]->getIdx() * 8],
                                *reg_z2x[used_reg]);
                } else if (reg_z2x[used_reg]->isYMM()) {
                    gen->vmovaps(
                        gen->ptr[context_reg + ymm_offs + reg_z2x[used_reg]->getIdx() * ymm_size],
                        *(Ymm*)reg_z2x[used_reg]);
                } else {
                    assert(false);
                }
            }

            if (UsesFlags) {
                gen->mov(gen->rax, context_base);
                gen->mov(rsp, ptr[gen->rax + host_rsp_offs]);
                gen->pushf();
                gen->pop(rcx);
                gen->mov(ptr[gen->rax + rflags_offs], rcx);
            }
        }

        runtime_address = next_address;
    }

    gen->int3();
    gen->int3();
    gen->int3();
    gen->int3();

    gen->ready();
    return Entry;
}

void GenerateTrampoline(u64 hle_handler, u64 context_base) {
    printf("Generating trampoline %llX\n", hle_handler);

    using namespace Xbyak;
    using namespace Xbyak::util;

    auto entry = translated_entries.find(hle_handler);
    if (entry == translated_entries.end()) {

        translated_entries[hle_handler] = gen->getCurr<PS4_SYSV_ABI u64 (*)()>();

        push_abi_regs();

        gen->mov(gen->rax, context_base);
        gen->mov(gen->qword[gen->rax + host_rsp_offs], gen->rsp);

        gen->mov(rax, context_base);

        // RSP
        gen->mov(rsp, ptr[rax + rsp.getIdx() * 8]);

        // pop & store original return address
        gen->pop(rdx);
        gen->mov(ptr[rax + trampoline_ret_offs], rdx);

        // args: RDI, RSI, RDX, RCX, R8, R9
        Reg64 args_64[] = {rdi, rsi, rdx, rcx, r8, r9};
        for (const auto& reg : args_64) {
            gen->mov(reg, ptr[rax + reg.getIdx() * 8]);
        }
        // args: XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6 and XMM7
        for (int i = 0; i < 8; i++) {
            gen->vmovaps(Ymm(i), ptr[rax + ymm_offs + i * ymm_size]);
        }

        gen->mov(rax, hle_handler);
        gen->call(rax); // this replaces the original return address

        // rets: RAX, RDX
        gen->mov(rcx, rax);
        Reg64 rets_64[] = {rcx /* rax is used as temp */, rdx};

        gen->mov(rax, context_base);
        gen->mov(ptr[rax + rax.getIdx() * 8], rets_64[0]);
        gen->mov(ptr[rax + rets_64[1].getIdx() * 8], rets_64[1]);

        // rets: XMM0
        gen->vmovaps(ptr[rax + ymm_offs + 0 * ymm_size], Ymm(0));

        // faux ret
        gen->mov(gen->rax, context_base + 4 * 8);
        gen->mov(gen->rsp, gen->qword[gen->rax]);
        gen->pop(gen->rcx);
        gen->mov(gen->qword[gen->rax], gen->rsp);

        gen->mov(gen->rax, context_base);
        gen->mov(gen->rsp, gen->qword[gen->rax + host_rsp_offs]);
        pop_abi_regs();

        gen->mov(gen->rax, ptr[gen->rax + trampoline_ret_offs]);
        gen->ret();
    }
}

static void RunMainEntry(VAddr addr, EntryParams* params, ExitFunc exit_func) {
    // reinterpret_cast<entry_func_t>(addr)(params, exit_func); // can't be used, stack has to have
    // a specific layout

    // Allocate stack for guest thread
    auto stack_top =
        8 * 1024 * 1024 + (u64)VirtualAlloc(0, 8 * 1024 * 1024, MEM_COMMIT, PAGE_READWRITE);

    {
        auto& rsp = thread_context.gpr[4];
        auto& rsi = thread_context.gpr[6];
        auto& rdi = thread_context.gpr[7];

        rsp = stack_top;

        rsp = rsp & ~16;
        rsp = rsp - 8;

        for (int i = params->argc; i > 0; i--) {
            rsp = rsp - 8;
            *(void**)rsp = &params->argv[i - 1];
        }

        rsp = rsp - 8;
        *(u64*)rsp = params->argc;

        rsi = (u64)params;
        rdi = (u64)exit_func;
    }

    thread_context.rip = addr;

    for (;;) {
        auto entry = translated_entries.find(thread_context.rip);
        if (entry == translated_entries.end()) {
            auto Entry = TranslateCode((u8*)thread_context.rip, (u64)&thread_context);
            translated_entries[thread_context.rip] = Entry;
            thread_context.rip = Entry();
        } else {
            thread_context.rip = entry->second();
        }
    }
}


static void RunMainEntryNative(VAddr addr, EntryParams* params, ExitFunc exit_func) {
    // reinterpret_cast<entry_func_t>(addr)(params, exit_func); // can't be used, stack has to have
    // a specific layout
    asm volatile("andq $-16, %%rsp\n" // Align to 16 bytes
                 "subq $8, %%rsp\n"   // videoout_basic expects the stack to be misaligned

                 // Kernel also pushes some more things here during process init
                 // at least: environment, auxv, possibly other things

                 "pushq 8(%1)\n" // copy EntryParams to top of stack like the kernel does
                 "pushq 0(%1)\n" // OpenOrbis expects to find it there

                 "movq %1, %%rdi\n" // also pass params and exit func
                 "movq %2, %%rsi\n" // as before

                 "jmp *%0\n" // can't use call here, as that would mangle the prepared stack.
                             // there's no coming back
                 :
                 : "r"(addr), "r"(params), "r"(exit_func)
                 : "rax", "rsi", "rdi");
}

Linker::Linker() : memory{Memory::Instance()} {}

Linker::~Linker() = default;

void Linker::Execute() {
    if (Config::debugDump()) {
        DebugDump();
    }

    // Calculate static TLS size.
    for (const auto& module : m_modules) {
        static_tls_size += module->tls.image_size;
        module->tls.offset = static_tls_size;
    }

    // Relocate all modules
    for (const auto& m : m_modules) {
        Relocate(m.get());
    }

    // Configure used flexible memory size.
    // if (auto* mem_param = GetProcParam()->mem_param) {
    //      if (u64* flexible_size = mem_param->flexible_memory_size) {
    //          memory->SetTotalFlexibleSize(*flexible_size);
    //      }
    //  }

    // Init primary thread.
    Common::SetCurrentThreadName("GAME_MainThread");
    Libraries::Kernel::pthreadInitSelfMainThread();
    InitTlsForThread(true);

    // Start shared library modules
    for (auto& m : m_modules) {
        if (m->IsSharedLib()) {
            m->Start(0, nullptr, nullptr);
        }
    }

    // Start main module.
    EntryParams p{};
    p.argc = 1;
    p.argv[0] = "eboot.bin";

    for (auto& m : m_modules) {
        if (!m->IsSharedLib()) {
            RunMainEntry(m->GetEntryAddress(), &p, ProgramExitFunc);
        }
    }
}

s32 Linker::LoadModule(const std::filesystem::path& elf_name, bool is_dynamic) {
    std::scoped_lock lk{mutex};

    if (!std::filesystem::exists(elf_name)) {
        LOG_ERROR(Core_Linker, "Provided file {} does not exist", elf_name.string());
        return -1;
    }

    auto module = std::make_unique<Module>(memory, elf_name, max_tls_index);
    if (!module->IsValid()) {
        LOG_ERROR(Core_Linker, "Provided file {} is not valid ELF file", elf_name.string());
        return -1;
    }

    num_static_modules += !is_dynamic;
    m_modules.emplace_back(std::move(module));
    return m_modules.size() - 1;
}

Module* Linker::FindByAddress(VAddr address) {
    for (auto& module : m_modules) {
        const VAddr base = module->GetBaseAddress();
        if (address >= base && address < base + module->aligned_base_size) {
            return module.get();
        }
    }
    return nullptr;
}

void Linker::Relocate(Module* module) {
    module->ForEachRelocation([&](elf_relocation* rel, u32 i, bool isJmpRel) {
        const u32 bit_idx =
            (isJmpRel ? module->dynamic_info.relocation_table_size / sizeof(elf_relocation) : 0) +
            i;
        if (module->TestRelaBit(bit_idx)) {
            return;
        }
        auto type = rel->GetType();
        auto symbol = rel->GetSymbol();
        auto addend = rel->rel_addend;
        auto* symbol_table = module->dynamic_info.symbol_table;
        auto* namesTlb = module->dynamic_info.str_table;

        const VAddr rel_base_virtual_addr = module->GetBaseAddress();
        const VAddr rel_virtual_addr = rel_base_virtual_addr + rel->rel_offset;
        bool rel_is_resolved = false;
        u64 rel_value = 0;
        Loader::SymbolType rel_sym_type = Loader::SymbolType::Unknown;
        std::string rel_name;

        switch (type) {
        case R_X86_64_RELATIVE:
            rel_value = rel_base_virtual_addr + addend;
            rel_is_resolved = true;
            module->SetRelaBit(bit_idx);
            break;
        case R_X86_64_DTPMOD64:
            rel_value = static_cast<u64>(module->tls.modid);
            rel_is_resolved = true;
            rel_sym_type = Loader::SymbolType::Tls;
            module->SetRelaBit(bit_idx);
            break;
        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT:
            addend = 0;
        case R_X86_64_64: {
            auto sym = symbol_table[symbol];
            auto sym_bind = sym.GetBind();
            auto sym_type = sym.GetType();
            auto sym_visibility = sym.GetVisibility();
            u64 symbol_vitrual_addr = 0;
            Loader::SymbolRecord symrec{};
            switch (sym_type) {
            case STT_FUN:
                rel_sym_type = Loader::SymbolType::Function;
                break;
            case STT_OBJECT:
                rel_sym_type = Loader::SymbolType::Object;
                break;
            case STT_NOTYPE:
                rel_sym_type = Loader::SymbolType::NoType;
                break;
            default:
                ASSERT_MSG(0, "unknown symbol type {}", sym_type);
            }

            if (sym_visibility != 0) {
                LOG_INFO(Core_Linker, "symbol visilibity !=0");
            }

            switch (sym_bind) {
            case STB_LOCAL:
                symbol_vitrual_addr = rel_base_virtual_addr + sym.st_value;
                module->SetRelaBit(bit_idx);
                break;
            case STB_GLOBAL:
            case STB_WEAK: {
                rel_name = namesTlb + sym.st_name;
                if (Resolve(rel_name, rel_sym_type, module, &symrec)) {
                    // Only set the rela bit if the symbol was actually resolved and not stubbed.
                    module->SetRelaBit(bit_idx);
                }
                symbol_vitrual_addr = symrec.virtual_address;
                break;
            }
            default:
                ASSERT_MSG(0, "unknown bind type {}", sym_bind);
            }
            rel_is_resolved = (symbol_vitrual_addr != 0);
            rel_value = (rel_is_resolved ? symbol_vitrual_addr + addend : 0);
            rel_name = symrec.name;
            break;
        }
        default:
            LOG_INFO(Core_Linker, "UNK type {:#010x} rel symbol : {:#010x}", type, symbol);
        }

        if (rel_is_resolved) {
            VirtualMemory::memory_patch(rel_virtual_addr, rel_value);
        } else {
            LOG_INFO(Core_Linker, "function not patched! {}", rel_name);
        }
    });
}

const Module* Linker::FindExportedModule(const ModuleInfo& module, const LibraryInfo& library) {
    const auto it = std::ranges::find_if(m_modules, [&](const auto& m) {
        return std::ranges::contains(m->GetExportLibs(), library) &&
               std::ranges::contains(m->GetExportModules(), module);
    });
    return it == m_modules.end() ? nullptr : it->get();
}

bool Linker::Resolve(const std::string& name, Loader::SymbolType sym_type, Module* m,
                     Loader::SymbolRecord* return_info) {
    const auto ids = Common::SplitString(name, '#');
    if (ids.size() != 3) {
        return_info->virtual_address = 0;
        return_info->name = name;
        LOG_ERROR(Core_Linker, "Not Resolved {}", name);
        return false;
    }

    const LibraryInfo* library = m->FindLibrary(ids[1]);
    const ModuleInfo* module = m->FindModule(ids[2]);
    ASSERT_MSG(library && module, "Unable to find library and module");

    Loader::SymbolResolver sr{};
    sr.name = ids.at(0);
    sr.library = library->name;
    sr.library_version = library->version;
    sr.module = module->name;
    sr.module_version_major = module->version_major;
    sr.module_version_minor = module->version_minor;
    sr.type = sym_type;

    const auto* record = m_hle_symbols.FindSymbol(sr);
    if (!record) {
        // Check if it an export function
        const auto* p = FindExportedModule(*module, *library);
        if (p && p->export_sym.GetSize() > 0) {
            record = p->export_sym.FindSymbol(sr);
        }
    }
    if (record) {
        *return_info = *record;
        GenerateTrampoline(return_info->virtual_address, (u64)&thread_context);
        return true;
    }

    const auto aeronid = AeroLib::FindByNid(sr.name.c_str());
    if (aeronid) {
        return_info->name = aeronid->name;
        return_info->virtual_address = AeroLib::GetStub(aeronid->nid);
    } else {
        return_info->virtual_address = AeroLib::GetStub(sr.name.c_str());
        return_info->name = "Unknown !!!";
    }
    LOG_ERROR(Core_Linker, "Linker: Stub resolved {} as {} (lib: {}, mod: {})", sr.name,
              return_info->name, library->name, module->name);
    GenerateTrampoline(return_info->virtual_address, (u64)&thread_context);
    return false;
}

void* Linker::TlsGetAddr(u64 module_index, u64 offset) {
    std::scoped_lock lk{mutex};

    DtvEntry* dtv_table = GetTcbBase()->tcb_dtv;
    if (dtv_table[0].counter != dtv_generation_counter) {
        // Generation counter changed, a dynamic module was either loaded or unloaded.
        const u32 old_num_dtvs = dtv_table[1].counter;
        ASSERT_MSG(max_tls_index > old_num_dtvs, "Module unloading unsupported");
        // Module was loaded, increase DTV table size.
        DtvEntry* new_dtv_table = new DtvEntry[max_tls_index + 2];
        std::memcpy(new_dtv_table + 2, dtv_table + 2, old_num_dtvs * sizeof(DtvEntry));
        new_dtv_table[0].counter = dtv_generation_counter;
        new_dtv_table[1].counter = max_tls_index;
        delete[] dtv_table;

        // Update TCB pointer.
        GetTcbBase()->tcb_dtv = new_dtv_table;
        dtv_table = new_dtv_table;
    }

    u8* addr = dtv_table[module_index + 1].pointer;
    if (!addr) {
        // Module was just loaded by above code. Allocate TLS block for it.
        Module* module = m_modules[module_index - 1].get();
        const u32 init_image_size = module->tls.init_image_size;
        u8* dest = reinterpret_cast<u8*>(heap_api_func(module->tls.image_size));
        const u8* src = reinterpret_cast<const u8*>(module->tls.image_virtual_addr);
        std::memcpy(dest, src, init_image_size);
        std::memset(dest + init_image_size, 0, module->tls.image_size - init_image_size);
        dtv_table[module_index + 1].pointer = dest;
        addr = dest;
    }
    return addr + offset;
}

void Linker::InitTlsForThread(bool is_primary) {
    static constexpr size_t TcbSize = 0x40;
    static constexpr size_t TlsAllocAlign = 0x20;
    const size_t total_tls_size = Common::AlignUp(static_tls_size, TlsAllocAlign) + TcbSize;

    // The kernel module has a few different paths for TLS allocation.
    // For SDK < 1.7 it allocates both main and secondary thread blocks using libc mspace/malloc.
    // In games compiled with newer SDK, the main thread gets mapped from flexible memory,
    // with addr = 0, so system managed area. Here we will only implement the latter.
    void* addr_out{};
    if (is_primary) {
        const size_t tls_aligned = Common::AlignUp(total_tls_size, 16_KB);
        const int ret = Libraries::Kernel::sceKernelMapNamedFlexibleMemory(
            &addr_out, tls_aligned, 3, 0, "SceKernelPrimaryTcbTls");
        ASSERT_MSG(ret == 0, "Unable to allocate TLS+TCB for the primary thread");
    } else {
        if (heap_api_func) {
            addr_out = heap_api_func(total_tls_size);
        } else {
            addr_out = std::malloc(total_tls_size);
        }
    }

    // Initialize allocated memory and allocate DTV table.
    const u32 num_dtvs = max_tls_index;
    std::memset(addr_out, 0, total_tls_size);
    DtvEntry* dtv_table = new DtvEntry[num_dtvs + 2];

    // Initialize thread control block
    u8* addr = reinterpret_cast<u8*>(addr_out);
    Tcb* tcb = reinterpret_cast<Tcb*>(addr + static_tls_size);
    tcb->tcb_self = tcb;
    tcb->tcb_dtv = dtv_table;

    // Dtv[0] is the generation counter. libkernel puts their number into dtv[1] (why?)
    dtv_table[0].counter = dtv_generation_counter;
    dtv_table[1].counter = num_dtvs;

    // Copy init images to TLS thread blocks and map them to DTV slots.
    for (u32 i = 0; i < num_static_modules; i++) {
        auto* module = m_modules[i].get();
        if (module->tls.image_size == 0) {
            continue;
        }
        u8* dest = reinterpret_cast<u8*>(addr + static_tls_size - module->tls.offset);
        const u8* src = reinterpret_cast<const u8*>(module->tls.image_virtual_addr);
        std::memcpy(dest, src, module->tls.init_image_size);
        tcb->tcb_dtv[module->tls.modid + 1].pointer = dest;
    }

    // Set pointer to FS base
    SetTcbBase(tcb);
}

void Linker::DebugDump() {
    const auto& log_dir = Common::FS::GetUserPath(Common::FS::PathType::LogDir);
    const std::filesystem::path debug(log_dir / "debugdump");
    std::filesystem::create_directory(debug);
    for (const auto& m : m_modules) {
        // TODO make a folder with game id for being more unique?
        const std::filesystem::path filepath(debug / m.get()->file.stem());
        std::filesystem::create_directory(filepath);
        m.get()->import_sym.DebugDump(filepath / "imports.txt");
        m.get()->export_sym.DebugDump(filepath / "exports.txt");
        if (m.get()->elf.IsSelfFile()) {
            m.get()->elf.SelfHeaderDebugDump(filepath / "selfHeader.txt");
            m.get()->elf.SelfSegHeaderDebugDump(filepath / "selfSegHeaders.txt");
        }
        m.get()->elf.ElfHeaderDebugDump(filepath / "elfHeader.txt");
        m.get()->elf.PHeaderDebugDump(filepath / "elfPHeaders.txt");
    }
}


} // namespace Core
