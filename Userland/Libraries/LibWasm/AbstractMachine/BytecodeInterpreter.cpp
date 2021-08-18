/*
 * Copyright (c) 2021, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <LibWasm/AbstractMachine/AbstractMachine.h>
#include <LibWasm/AbstractMachine/BytecodeInterpreter.h>
#include <LibWasm/AbstractMachine/Configuration.h>
#include <LibWasm/AbstractMachine/Operators.h>
#include <LibWasm/Opcode.h>
#include <LibWasm/Printer/Printer.h>

namespace Wasm {

void BytecodeInterpreter::interpret(Configuration& configuration)
{
    m_trap.clear();
    auto& instructions = configuration.frame().expression().instructions();
    auto max_ip_value = InstructionPointer { instructions.size() };
    auto& current_ip_value = configuration.ip();
    auto const should_limit_instruction_count = configuration.should_limit_instruction_count();
    u64 executed_instructions = 0;

    while (current_ip_value < max_ip_value) {
        if (should_limit_instruction_count) {
            if (executed_instructions++ >= Constants::max_allowed_executed_instructions_per_call) [[unlikely]] {
                m_trap = Trap { "Exceeded maximum allowed number of instructions" };
                return;
            }
        }
        auto& instruction = instructions[current_ip_value.value()];
        auto old_ip = current_ip_value;
        interpret(configuration, current_ip_value, instruction);
        if (m_trap.has_value())
            return;
        if (current_ip_value == old_ip) // If no jump occurred
            ++current_ip_value;
    }
}

constexpr auto BytecodeInterpreter::generate_single_byte_jump_table()
{
    using MemberFunctionType = decltype(&BytecodeInterpreter::unimplemented_impl);
    Array<MemberFunctionType, 0xff> result;
    for (size_t i = 0; i < 0xff; ++i)
        result[i] = &BytecodeInterpreter::unimplemented_impl;

#define M(index, name) result[index] = &BytecodeInterpreter::name##_impl;

    ENUMERATE_WASM_SINGLE_BYTE_INSTRUCTIONS(M)

#undef M
    return result;
}

void BytecodeInterpreter::interpret(Configuration& configuration, InstructionPointer& ip, Instruction const& instruction)
{
    dbgln_if(WASM_TRACE_DEBUG, "Executing instruction {} at ip {}", instruction_name(instruction.opcode()), ip.value());

    static constexpr auto s_single_byte_opcode_table = generate_single_byte_jump_table();

    if (instruction.opcode().value() <= 0xff)
        return (this->*s_single_byte_opcode_table[instruction.opcode().value()])(configuration, ip, instruction);

#define M(value, name) \
    case value:        \
        return name##_impl(configuration, ip, instruction);

    switch (instruction.opcode().value()) {
        ENUMERATE_WASM_MULTI_BYTE_INSTRUCTIONS(M)
    }

#undef M

}

void DebuggerBytecodeInterpreter::interpret(Configuration& configuration, InstructionPointer& ip, Instruction const& instruction)
{
    if (pre_interpret_hook) {
        auto result = pre_interpret_hook(configuration, ip, instruction);
        if (!result) {
            m_trap = Trap { "Trapped by user request" };
            return;
        }
    }

    BytecodeInterpreter::interpret(configuration, ip, instruction);

    if (post_interpret_hook) {
        auto result = post_interpret_hook(configuration, ip, instruction, *this);
        if (!result) {
            m_trap = Trap { "Trapped by user request" };
            return;
        }
    }
}
}
