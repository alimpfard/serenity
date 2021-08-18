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

void BytecodeInterpreter::branch_to_label(Configuration& configuration, LabelIndex index)
{
    dbgln_if(WASM_TRACE_DEBUG, "Branch to label with index {}...", index.value());
    auto label = configuration.nth_label(index.value());
    TRAP_IF_NOT(label.has_value());
    dbgln_if(WASM_TRACE_DEBUG, "...which is actually IP {}, and has {} result(s)", label->continuation().value(), label->arity());
    auto results = pop_values(configuration, label->arity());

    size_t drop_count = index.value() + 1;
    for (; !configuration.stack().is_empty();) {
        auto& entry = configuration.stack().peek();
        if (entry.has<Label>()) {
            if (--drop_count == 0)
                break;
        }
        configuration.stack().pop();
    }

    for (auto& result : results)
        configuration.stack().push(move(result));

    configuration.ip() = label->continuation();
}

template<typename ReadType, typename PushType>
void BytecodeInterpreter::load_and_push(Configuration& configuration, Instruction const& instruction)
{
    auto& address = configuration.frame().module().memories().first();
    auto memory = configuration.store().get(address);
    if (!memory) {
        m_trap = Trap { "Nonexistent memory" };
        return;
    }
    auto& arg = instruction.arguments().get<Instruction::MemoryArgument>();
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto& entry = configuration.stack().peek();
    TRAP_IF_NOT(entry.has<Value>());
    auto base = entry.get<Value>().to<i32>();
    if (!base.has_value()) {
        m_trap = Trap { "Memory access out of bounds" };
        return;
    }
    u64 instance_address = static_cast<u64>(bit_cast<u32>(base.value())) + arg.offset;
    Checked addition { instance_address };
    addition += sizeof(ReadType);
    if (addition.has_overflow() || addition.value() > memory->size()) {
        m_trap = Trap { "Memory access out of bounds" };
        dbgln("LibWasm: Memory access out of bounds (expected {} to be less than or equal to {})", instance_address + sizeof(ReadType), memory->size());
        return;
    }
    dbgln_if(WASM_TRACE_DEBUG, "load({} : {}) -> stack", instance_address, sizeof(ReadType));
    auto slice = memory->data().bytes().slice(instance_address, sizeof(ReadType));
    configuration.stack().peek() = Value(static_cast<PushType>(read_value<ReadType>(slice)));
}

void BytecodeInterpreter::call_address(Configuration& configuration, FunctionAddress address)
{
    TRAP_IF_NOT(m_stack_info.size_free() >= Constants::minimum_stack_space_to_keep_free);

    auto instance = configuration.store().get(address);
    TRAP_IF_NOT(instance);
    FunctionType const* type { nullptr };
    instance->visit([&](auto const& function) { type = &function.type(); });
    TRAP_IF_NOT(type);
    TRAP_IF_NOT(configuration.stack().entries().size() > type->parameters().size());
    Vector<Value> args;
    args.ensure_capacity(type->parameters().size());
    auto span = configuration.stack().entries().span().slice_from_end(type->parameters().size());
    for (auto& entry : span) {
        auto* ptr = entry.get_pointer<Value>();
        TRAP_IF_NOT(ptr != nullptr);
        args.unchecked_append(move(*ptr));
    }

    configuration.stack().entries().remove(configuration.stack().size() - span.size(), span.size());

    Result result { Trap { ""sv } };
    {
        CallFrameHandle handle { *this, configuration };
        result = configuration.call(*this, address, move(args));
    }

    if (result.is_trap()) {
        m_trap = move(result.trap());
        return;
    }

    configuration.stack().entries().ensure_capacity(configuration.stack().size() + result.values().size());
    for (auto& entry : result.values())
        configuration.stack().entries().unchecked_append(move(entry));
}

template<typename PopType, typename PushType, typename Operator>
void BytecodeInterpreter::binary_numeric_operation(Configuration& configuration)
{
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto rhs_entry = configuration.stack().pop();
    auto& lhs_entry = configuration.stack().peek();
    auto rhs_ptr = rhs_entry.get_pointer<Value>();
    auto lhs_ptr = lhs_entry.get_pointer<Value>();
    TRAP_IF_NOT(rhs_ptr);
    TRAP_IF_NOT(lhs_ptr);
    auto rhs = rhs_ptr->to<PopType>();
    auto lhs = lhs_ptr->to<PopType>();
    TRAP_IF_NOT(lhs.has_value());
    TRAP_IF_NOT(rhs.has_value());
    PushType result;
    auto call_result = Operator {}(lhs.value(), rhs.value());
    if constexpr (IsSpecializationOf<decltype(call_result), AK::Result>) {
        if (call_result.is_error()) [[unlikely]] {
            trap_if_not(false, call_result.error());
            return;
        }
        result = call_result.release_value();
    } else {
        result = call_result;
    }
    dbgln_if(WASM_TRACE_DEBUG, "{} {} {} = {}", lhs.value(), Operator::name(), rhs.value(), result);
    lhs_entry = Value(result);
}

template<typename PopType, typename PushType, typename Operator>
void BytecodeInterpreter::unary_operation(Configuration& configuration)
{
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto& entry = configuration.stack().peek();
    auto entry_ptr = entry.get_pointer<Value>();
    TRAP_IF_NOT(entry_ptr);
    auto value = entry_ptr->to<PopType>();
    TRAP_IF_NOT(value.has_value());
    auto call_result = Operator {}(*value);
    PushType result;
    if constexpr (IsSpecializationOf<decltype(call_result), AK::Result>) {
        if (call_result.is_error()) {
            trap_if_not(false, call_result.error());
            return;
        }
        result = call_result.release_value();
    } else {
        result = call_result;
    }
    dbgln_if(WASM_TRACE_DEBUG, "map({}) {} = {}", Operator::name(), *value, result);
    entry = Value(result);
}

template<typename T>
struct ConvertToRaw {
    T operator()(T value)
    {
        return LittleEndian<T>(value);
    }
};

template<>
struct ConvertToRaw<float> {
    u32 operator()(float value)
    {
        LittleEndian<u32> res;
        ReadonlyBytes bytes { &value, sizeof(float) };
        InputMemoryStream stream { bytes };
        stream >> res;
        VERIFY(!stream.has_any_error());
        return static_cast<u32>(res);
    }
};

template<>
struct ConvertToRaw<double> {
    u64 operator()(double value)
    {
        LittleEndian<u64> res;
        ReadonlyBytes bytes { &value, sizeof(double) };
        InputMemoryStream stream { bytes };
        stream >> res;
        VERIFY(!stream.has_any_error());
        return static_cast<u64>(res);
    }
};

template<typename PopT, typename StoreT>
void BytecodeInterpreter::pop_and_store(Configuration& configuration, Instruction const& instruction)
{
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto entry = configuration.stack().pop();
    TRAP_IF_NOT(entry.has<Value>());
    auto value = ConvertToRaw<StoreT> {}(*entry.get<Value>().to<PopT>());
    dbgln_if(WASM_TRACE_DEBUG, "stack({}) -> temporary({}b)", value, sizeof(StoreT));
    store_to_memory(configuration, instruction, { &value, sizeof(StoreT) });
}

void BytecodeInterpreter::store_to_memory(Configuration& configuration, Instruction const& instruction, ReadonlyBytes data)
{
    auto& address = configuration.frame().module().memories().first();
    auto memory = configuration.store().get(address);
    TRAP_IF_NOT(memory);
    auto& arg = instruction.arguments().get<Instruction::MemoryArgument>();
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto entry = configuration.stack().pop();
    TRAP_IF_NOT(entry.has<Value>());
    auto base = entry.get<Value>().to<i32>();
    TRAP_IF_NOT(base.has_value());
    u64 instance_address = static_cast<u64>(bit_cast<u32>(base.value())) + arg.offset;
    Checked addition { instance_address };
    addition += data.size();
    if (addition.has_overflow() || addition.value() > memory->size()) {
        m_trap = Trap { "Memory access out of bounds" };
        dbgln("LibWasm: Memory access out of bounds (expected 0 <= {} and {} <= {})", instance_address, instance_address + data.size(), memory->size());
        return;
    }
    dbgln_if(WASM_TRACE_DEBUG, "tempoaray({}b) -> store({})", data.size(), instance_address);
    data.copy_to(memory->data().bytes().slice(instance_address, data.size()));
}

template<typename T>
T BytecodeInterpreter::read_value(ReadonlyBytes data)
{
    LittleEndian<T> value;
    InputMemoryStream stream { data };
    stream >> value;
    if (stream.handle_any_error()) {
        dbgln("Read from {} failed", data.data());
        m_trap = Trap { "Read from memory failed" };
    }
    return value;
}

template<>
float BytecodeInterpreter::read_value<float>(ReadonlyBytes data)
{
    InputMemoryStream stream { data };
    LittleEndian<u32> raw_value;
    stream >> raw_value;
    if (stream.handle_any_error())
        m_trap = Trap { "Read from memory failed" };
    return bit_cast<float>(static_cast<u32>(raw_value));
}

template<>
double BytecodeInterpreter::read_value<double>(ReadonlyBytes data)
{
    InputMemoryStream stream { data };
    LittleEndian<u64> raw_value;
    stream >> raw_value;
    if (stream.handle_any_error())
        m_trap = Trap { "Read from memory failed" };
    return bit_cast<double>(static_cast<u64>(raw_value));
}

template<typename V, typename T>
MakeSigned<T> BytecodeInterpreter::checked_signed_truncate(V value)
{
    if (isnan(value) || isinf(value)) { // "undefined", let's just trap.
        m_trap = Trap { "Signed truncation undefined behaviour" };
        return 0;
    }

    double truncated;
    if constexpr (IsSame<float, V>)
        truncated = truncf(value);
    else
        truncated = trunc(value);

    using SignedT = MakeSigned<T>;
    if (NumericLimits<SignedT>::min() <= truncated && static_cast<double>(NumericLimits<SignedT>::max()) >= truncated)
        return static_cast<SignedT>(truncated);

    dbgln_if(WASM_TRACE_DEBUG, "Truncate out of range error");
    m_trap = Trap { "Signed truncation out of range" };
    return true;
}

template<typename V, typename T>
MakeUnsigned<T> BytecodeInterpreter::checked_unsigned_truncate(V value)
{
    if (isnan(value) || isinf(value)) { // "undefined", let's just trap.
        m_trap = Trap { "Unsigned truncation undefined behaviour" };
        return 0;
    }
    double truncated;
    if constexpr (IsSame<float, V>)
        truncated = truncf(value);
    else
        truncated = trunc(value);

    using UnsignedT = MakeUnsigned<T>;
    if (NumericLimits<UnsignedT>::min() <= truncated && static_cast<double>(NumericLimits<UnsignedT>::max()) >= truncated)
        return static_cast<UnsignedT>(truncated);

    dbgln_if(WASM_TRACE_DEBUG, "Truncate out of range error");
    m_trap = Trap { "Unsigned truncation out of range" };
    return true;
}

Vector<Value> BytecodeInterpreter::pop_values(Configuration& configuration, size_t count)
{
    Vector<Value> results;
    results.resize(count);

    for (size_t i = 0; i < count; ++i) {
        auto top_of_stack = configuration.stack().pop();
        if (auto value = top_of_stack.get_pointer<Value>())
            results[i] = move(*value);
        else
            TRAP_IF_NOT_NORETURN(value);
    }
    return results;
}

void BytecodeInterpreter::unreachable_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    m_trap = Trap { "Unreachable" };
}
void BytecodeInterpreter::nop_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
}
void BytecodeInterpreter::local_get_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    configuration.stack().push(Value(configuration.frame().locals()[instruction.arguments().get<LocalIndex>().value()]));
}
void BytecodeInterpreter::local_set_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto entry = configuration.stack().pop();
    TRAP_IF_NOT(entry.has<Value>());
    configuration.frame().locals()[instruction.arguments().get<LocalIndex>().value()] = move(entry.get<Value>());
}
void BytecodeInterpreter::i32_const_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    configuration.stack().push(Value(ValueType { ValueType::I32 }, static_cast<i64>(instruction.arguments().get<i32>())));
}
void BytecodeInterpreter::i64_const_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    configuration.stack().push(Value(ValueType { ValueType::I64 }, instruction.arguments().get<i64>()));
}
void BytecodeInterpreter::f32_const_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    configuration.stack().push(Value(ValueType { ValueType::F32 }, static_cast<double>(instruction.arguments().get<float>())));
}
void BytecodeInterpreter::f64_const_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    configuration.stack().push(Value(ValueType { ValueType::F64 }, instruction.arguments().get<double>()));
}
void BytecodeInterpreter::block_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    size_t arity = 0;
    auto& args = instruction.arguments().get<Instruction::StructuredInstructionArgs>();
    if (args.block_type.kind() != BlockType::Empty)
        arity = 1;
    configuration.stack().push(Label(arity, args.end_ip));
}
void BytecodeInterpreter::loop_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    size_t arity = 0;
    auto& args = instruction.arguments().get<Instruction::StructuredInstructionArgs>();
    if (args.block_type.kind() != BlockType::Empty)
        arity = 1;
    configuration.stack().push(Label(arity, ip.value() + 1));
}
void BytecodeInterpreter::if__impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    size_t arity = 0;
    auto& args = instruction.arguments().get<Instruction::StructuredInstructionArgs>();
    if (args.block_type.kind() != BlockType::Empty)
        arity = 1;

    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto entry = configuration.stack().pop();
    TRAP_IF_NOT(entry.has<Value>());
    auto value = entry.get<Value>().to<i32>();
    TRAP_IF_NOT(value.has_value());
    auto end_label = Label(arity, args.end_ip.value());
    if (value.value() == 0) {
        if (args.else_ip.has_value()) {
            configuration.ip() = args.else_ip.value();
            configuration.stack().push(move(end_label));
        } else {
            configuration.ip() = args.end_ip.value() + 1;
        }
    } else {
        configuration.stack().push(move(end_label));
    }
}
void BytecodeInterpreter::structured_end_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    auto index = configuration.nth_label_index(0);
    TRAP_IF_NOT(index.has_value());
    configuration.stack().entries().remove(*index, 1);
}

void BytecodeInterpreter::structured_else_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    auto index = configuration.nth_label_index(0);
    TRAP_IF_NOT(index.has_value());
    auto label = configuration.stack().entries()[*index].get<Label>();
    configuration.stack().entries().remove(*index, 1);
    // Jump to the end label
    configuration.ip() = label.continuation();
}

void BytecodeInterpreter::return__impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    auto& frame = configuration.frame();
    size_t end = configuration.stack().size() - frame.arity();
    size_t start = end;
    for (; start + 1 > 0 && start < configuration.stack().size(); --start) {
        auto& entry = configuration.stack().entries()[start];
        if (entry.has<Frame>()) {
            // Leave the frame, _and_ its label.
            start += 2;
            break;
        }
    }

    configuration.stack().entries().remove(start, end - start);

    // Jump past the call/indirect instruction
    configuration.ip() = configuration.frame().expression().instructions().size();
}
void BytecodeInterpreter::br_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return branch_to_label(configuration, instruction.arguments().get<LabelIndex>());
}

void BytecodeInterpreter::br_if_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto entry = configuration.stack().pop();
    TRAP_IF_NOT(entry.has<Value>());
    if (entry.get<Value>().to<i32>().value_or(0) == 0)
        return;
    return branch_to_label(configuration, instruction.arguments().get<LabelIndex>());
}
void BytecodeInterpreter::br_table_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    auto& arguments = instruction.arguments().get<Instruction::TableBranchArgs>();
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto entry = configuration.stack().pop();
    TRAP_IF_NOT(entry.has<Value>());
    auto maybe_i = entry.get<Value>().to<i32>();
    TRAP_IF_NOT(maybe_i.has_value());
    if (0 <= *maybe_i) {
        size_t i = *maybe_i;
        if (i < arguments.labels.size())
            return branch_to_label(configuration, arguments.labels[i]);
    }
    return branch_to_label(configuration, arguments.default_);
}
void BytecodeInterpreter::call_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    auto index = instruction.arguments().get<FunctionIndex>();
    TRAP_IF_NOT(index.value() < configuration.frame().module().functions().size());
    auto address = configuration.frame().module().functions()[index.value()];
    dbgln_if(WASM_TRACE_DEBUG, "call({})", address.value());
    call_address(configuration, address);
}
void BytecodeInterpreter::call_indirect_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    auto& args = instruction.arguments().get<Instruction::IndirectCallArgs>();
    TRAP_IF_NOT(args.table.value() < configuration.frame().module().tables().size());
    auto table_address = configuration.frame().module().tables()[args.table.value()];
    auto table_instance = configuration.store().get(table_address);
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto entry = configuration.stack().pop();
    TRAP_IF_NOT(entry.has<Value>());
    auto index = entry.get<Value>().to<i32>();
    TRAP_IF_NOT(index.has_value());
    TRAP_IF_NOT(index.value() >= 0);
    TRAP_IF_NOT(static_cast<size_t>(index.value()) < table_instance->elements().size());
    auto element = table_instance->elements()[index.value()];
    TRAP_IF_NOT(element.has_value());
    TRAP_IF_NOT(element->ref().has<Reference::Func>());
    auto address = element->ref().get<Reference::Func>().address;
    dbgln_if(WASM_TRACE_DEBUG, "call_indirect({} -> {})", index.value(), address.value());
    call_address(configuration, address);
}
void BytecodeInterpreter::i32_load_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<i32, i32>(configuration, instruction);
}
void BytecodeInterpreter::i64_load_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<i64, i64>(configuration, instruction);
}
void BytecodeInterpreter::f32_load_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<float, float>(configuration, instruction);
}
void BytecodeInterpreter::f64_load_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<double, double>(configuration, instruction);
}
void BytecodeInterpreter::i32_load8_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<i8, i32>(configuration, instruction);
}
void BytecodeInterpreter::i32_load8_u_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<u8, i32>(configuration, instruction);
}
void BytecodeInterpreter::i32_load16_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<i16, i32>(configuration, instruction);
}
void BytecodeInterpreter::i32_load16_u_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<u16, i32>(configuration, instruction);
}
void BytecodeInterpreter::i64_load8_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<i8, i64>(configuration, instruction);
}
void BytecodeInterpreter::i64_load8_u_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<u8, i64>(configuration, instruction);
}
void BytecodeInterpreter::i64_load16_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<i16, i64>(configuration, instruction);
}
void BytecodeInterpreter::i64_load16_u_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<u16, i64>(configuration, instruction);
}
void BytecodeInterpreter::i64_load32_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<i32, i64>(configuration, instruction);
}
void BytecodeInterpreter::i64_load32_u_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return load_and_push<u32, i64>(configuration, instruction);
}
void BytecodeInterpreter::i32_store_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return pop_and_store<i32, i32>(configuration, instruction);
}
void BytecodeInterpreter::i64_store_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return pop_and_store<i64, i64>(configuration, instruction);
}
void BytecodeInterpreter::f32_store_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return pop_and_store<float, float>(configuration, instruction);
}
void BytecodeInterpreter::f64_store_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return pop_and_store<double, double>(configuration, instruction);
}
void BytecodeInterpreter::i32_store8_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return pop_and_store<i32, i8>(configuration, instruction);
}
void BytecodeInterpreter::i32_store16_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return pop_and_store<i32, i16>(configuration, instruction);
}
void BytecodeInterpreter::i64_store8_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return pop_and_store<i64, i8>(configuration, instruction);
}
void BytecodeInterpreter::i64_store16_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return pop_and_store<i64, i16>(configuration, instruction);
}
void BytecodeInterpreter::i64_store32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return pop_and_store<i64, i32>(configuration, instruction);
}
void BytecodeInterpreter::local_tee_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto& entry = configuration.stack().peek();
    TRAP_IF_NOT(entry.has<Value>());
    auto value = entry.get<Value>();
    auto local_index = instruction.arguments().get<LocalIndex>();
    TRAP_IF_NOT(configuration.frame().locals().size() > local_index.value());
    dbgln_if(WASM_TRACE_DEBUG, "stack:peek -> locals({})", local_index.value());
    configuration.frame().locals()[local_index.value()] = move(value);
}
void BytecodeInterpreter::global_get_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    auto global_index = instruction.arguments().get<GlobalIndex>();
    TRAP_IF_NOT(configuration.frame().module().globals().size() > global_index.value());
    auto address = configuration.frame().module().globals()[global_index.value()];
    dbgln_if(WASM_TRACE_DEBUG, "global({}) -> stack", address.value());
    auto global = configuration.store().get(address);
    configuration.stack().push(Value(global->value()));
}
void BytecodeInterpreter::global_set_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    auto global_index = instruction.arguments().get<GlobalIndex>();
    TRAP_IF_NOT(configuration.frame().module().globals().size() > global_index.value());
    auto address = configuration.frame().module().globals()[global_index.value()];
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto entry = configuration.stack().pop();
    TRAP_IF_NOT(entry.has<Value>());
    auto value = entry.get<Value>();
    dbgln_if(WASM_TRACE_DEBUG, "stack -> global({})", address.value());
    auto global = configuration.store().get(address);
    global->set_value(move(value));
}
void BytecodeInterpreter::memory_size_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    TRAP_IF_NOT(configuration.frame().module().memories().size() > 0);
    auto address = configuration.frame().module().memories()[0];
    auto instance = configuration.store().get(address);
    auto pages = instance->size() / Constants::page_size;
    dbgln_if(WASM_TRACE_DEBUG, "memory.size -> stack({})", pages);
    configuration.stack().push(Value((i32)pages));
}
void BytecodeInterpreter::memory_grow_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    TRAP_IF_NOT(configuration.frame().module().memories().size() > 0);
    auto address = configuration.frame().module().memories()[0];
    auto instance = configuration.store().get(address);
    i32 old_pages = instance->size() / Constants::page_size;
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto& entry = configuration.stack().peek();
    TRAP_IF_NOT(entry.has<Value>());
    auto new_pages = entry.get<Value>().to<i32>();
    TRAP_IF_NOT(new_pages.has_value());
    dbgln_if(WASM_TRACE_DEBUG, "memory.grow({}), previously {} pages...", *new_pages, old_pages);
    if (instance->grow(new_pages.value() * Constants::page_size))
        configuration.stack().peek() = Value((i32)old_pages);
    else
        configuration.stack().peek() = Value((i32)-1);
}
void BytecodeInterpreter::ref_null_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    auto type = instruction.arguments().get<ValueType>();
    TRAP_IF_NOT(type.is_reference());
    configuration.stack().push(Value(Reference(Reference::Null { type })));
}
void BytecodeInterpreter::ref_func_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    auto index = instruction.arguments().get<FunctionIndex>().value();
    auto& functions = configuration.frame().module().functions();
    TRAP_IF_NOT(functions.size() > index);
    auto address = functions[index];
    configuration.stack().push(Value(ValueType(ValueType::FunctionReference), address.value()));
}
void BytecodeInterpreter::ref_is_null_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto top = configuration.stack().peek().get_pointer<Value>();
    TRAP_IF_NOT(top);
    TRAP_IF_NOT(top->type().is_reference());
    auto is_null = top->to<Reference::Null>().has_value();
    configuration.stack().peek() = Value(ValueType(ValueType::I32), static_cast<u64>(is_null ? 1 : 0));
}
void BytecodeInterpreter::drop_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    TRAP_IF_NOT(!configuration.stack().is_empty());
    configuration.stack().pop();
}
void BytecodeInterpreter::select_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return select_typed_impl(configuration, ip, instruction);
}
void BytecodeInterpreter::select_typed_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    // Note: The type seems to only be used for validation.
    TRAP_IF_NOT(!configuration.stack().is_empty());
    auto entry = configuration.stack().pop();
    TRAP_IF_NOT(entry.has<Value>());
    auto value = entry.get<Value>().to<i32>();
    TRAP_IF_NOT(value.has_value());
    dbgln_if(WASM_TRACE_DEBUG, "select({})", value.value());
    auto rhs_entry = configuration.stack().pop();
    TRAP_IF_NOT(rhs_entry.has<Value>());
    auto& lhs_entry = configuration.stack().peek();
    TRAP_IF_NOT(lhs_entry.has<Value>());
    auto rhs = move(rhs_entry.get<Value>());
    auto lhs = move(lhs_entry.get<Value>());
    configuration.stack().peek() = value.value() != 0 ? move(lhs) : move(rhs);
}
void BytecodeInterpreter::i32_eqz_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i32, i32, Operators::EqualsZero>(configuration);
}
void BytecodeInterpreter::i32_eq_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i32, i32, Operators::Equals>(configuration);
}
void BytecodeInterpreter::i32_ne_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i32, i32, Operators::NotEquals>(configuration);
}
void BytecodeInterpreter::i32_lts_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i32, i32, Operators::LessThan>(configuration);
}
void BytecodeInterpreter::i32_ltu_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::LessThan>(configuration);
}
void BytecodeInterpreter::i32_gts_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i32, i32, Operators::GreaterThan>(configuration);
}
void BytecodeInterpreter::i32_gtu_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::GreaterThan>(configuration);
}
void BytecodeInterpreter::i32_les_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i32, i32, Operators::LessThanOrEquals>(configuration);
}
void BytecodeInterpreter::i32_leu_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::LessThanOrEquals>(configuration);
}
void BytecodeInterpreter::i32_ges_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i32, i32, Operators::GreaterThanOrEquals>(configuration);
}
void BytecodeInterpreter::i32_geu_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::GreaterThanOrEquals>(configuration);
}
void BytecodeInterpreter::i64_eqz_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i64, i32, Operators::EqualsZero>(configuration);
}
void BytecodeInterpreter::i64_eq_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i64, i32, Operators::Equals>(configuration);
}
void BytecodeInterpreter::i64_ne_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i64, i32, Operators::NotEquals>(configuration);
}
void BytecodeInterpreter::i64_lts_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i64, i32, Operators::LessThan>(configuration);
}
void BytecodeInterpreter::i64_ltu_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i32, Operators::LessThan>(configuration);
}
void BytecodeInterpreter::i64_gts_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i64, i32, Operators::GreaterThan>(configuration);
}
void BytecodeInterpreter::i64_gtu_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i32, Operators::GreaterThan>(configuration);
}
void BytecodeInterpreter::i64_les_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i64, i32, Operators::LessThanOrEquals>(configuration);
}
void BytecodeInterpreter::i64_leu_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i32, Operators::LessThanOrEquals>(configuration);
}
void BytecodeInterpreter::i64_ges_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i64, i32, Operators::GreaterThanOrEquals>(configuration);
}
void BytecodeInterpreter::i64_geu_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i32, Operators::GreaterThanOrEquals>(configuration);
}
void BytecodeInterpreter::f32_eq_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, i32, Operators::Equals>(configuration);
}
void BytecodeInterpreter::f32_ne_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, i32, Operators::NotEquals>(configuration);
}
void BytecodeInterpreter::f32_lt_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, i32, Operators::LessThan>(configuration);
}
void BytecodeInterpreter::f32_gt_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, i32, Operators::GreaterThan>(configuration);
}
void BytecodeInterpreter::f32_le_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, i32, Operators::LessThanOrEquals>(configuration);
}
void BytecodeInterpreter::f32_ge_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, i32, Operators::GreaterThanOrEquals>(configuration);
}
void BytecodeInterpreter::f64_eq_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, i32, Operators::Equals>(configuration);
}
void BytecodeInterpreter::f64_ne_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, i32, Operators::NotEquals>(configuration);
}
void BytecodeInterpreter::f64_lt_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, i32, Operators::LessThan>(configuration);
}
void BytecodeInterpreter::f64_gt_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, i32, Operators::GreaterThan>(configuration);
}
void BytecodeInterpreter::f64_le_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, i32, Operators::LessThanOrEquals>(configuration);
}
void BytecodeInterpreter::f64_ge_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, i32, Operators::GreaterThanOrEquals>(configuration);
}
void BytecodeInterpreter::i32_clz_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i32, i32, Operators::CountLeadingZeros>(configuration);
}
void BytecodeInterpreter::i32_ctz_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i32, i32, Operators::CountTrailingZeros>(configuration);
}
void BytecodeInterpreter::i32_popcnt_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i32, i32, Operators::PopCount>(configuration);
}
void BytecodeInterpreter::i32_add_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::Add>(configuration);
}
void BytecodeInterpreter::i32_sub_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::Subtract>(configuration);
}
void BytecodeInterpreter::i32_mul_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::Multiply>(configuration);
}
void BytecodeInterpreter::i32_divs_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i32, i32, Operators::Divide>(configuration);
}
void BytecodeInterpreter::i32_divu_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::Divide>(configuration);
}
void BytecodeInterpreter::i32_rems_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i32, i32, Operators::Modulo>(configuration);
}
void BytecodeInterpreter::i32_remu_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::Modulo>(configuration);
}
void BytecodeInterpreter::i32_and_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i32, i32, Operators::BitAnd>(configuration);
}
void BytecodeInterpreter::i32_or_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i32, i32, Operators::BitOr>(configuration);
}
void BytecodeInterpreter::i32_xor_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i32, i32, Operators::BitXor>(configuration);
}
void BytecodeInterpreter::i32_shl_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::BitShiftLeft>(configuration);
}
void BytecodeInterpreter::i32_shrs_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i32, i32, Operators::BitShiftRight>(configuration);
}
void BytecodeInterpreter::i32_shru_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::BitShiftRight>(configuration);
}
void BytecodeInterpreter::i32_rotl_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::BitRotateLeft>(configuration);
}
void BytecodeInterpreter::i32_rotr_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u32, i32, Operators::BitRotateRight>(configuration);
}
void BytecodeInterpreter::i64_clz_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i64, i64, Operators::CountLeadingZeros>(configuration);
}
void BytecodeInterpreter::i64_ctz_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i64, i64, Operators::CountTrailingZeros>(configuration);
}
void BytecodeInterpreter::i64_popcnt_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i64, i64, Operators::PopCount>(configuration);
}
void BytecodeInterpreter::i64_add_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i64, Operators::Add>(configuration);
}
void BytecodeInterpreter::i64_sub_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i64, Operators::Subtract>(configuration);
}
void BytecodeInterpreter::i64_mul_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i64, Operators::Multiply>(configuration);
}
void BytecodeInterpreter::i64_divs_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i64, i64, Operators::Divide>(configuration);
}
void BytecodeInterpreter::i64_divu_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i64, Operators::Divide>(configuration);
}
void BytecodeInterpreter::i64_rems_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i64, i64, Operators::Modulo>(configuration);
}
void BytecodeInterpreter::i64_remu_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i64, Operators::Modulo>(configuration);
}
void BytecodeInterpreter::i64_and_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i64, i64, Operators::BitAnd>(configuration);
}
void BytecodeInterpreter::i64_or_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i64, i64, Operators::BitOr>(configuration);
}
void BytecodeInterpreter::i64_xor_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i64, i64, Operators::BitXor>(configuration);
}
void BytecodeInterpreter::i64_shl_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i64, Operators::BitShiftLeft>(configuration);
}
void BytecodeInterpreter::i64_shrs_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<i64, i64, Operators::BitShiftRight>(configuration);
}
void BytecodeInterpreter::i64_shru_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i64, Operators::BitShiftRight>(configuration);
}
void BytecodeInterpreter::i64_rotl_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i64, Operators::BitRotateLeft>(configuration);
}
void BytecodeInterpreter::i64_rotr_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<u64, i64, Operators::BitRotateRight>(configuration);
}
void BytecodeInterpreter::f32_abs_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, float, Operators::Absolute>(configuration);
}
void BytecodeInterpreter::f32_neg_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, float, Operators::Negate>(configuration);
}
void BytecodeInterpreter::f32_ceil_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, float, Operators::Ceil>(configuration);
}
void BytecodeInterpreter::f32_floor_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, float, Operators::Floor>(configuration);
}
void BytecodeInterpreter::f32_trunc_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, float, Operators::Truncate>(configuration);
}
void BytecodeInterpreter::f32_nearest_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, float, Operators::Round>(configuration);
}
void BytecodeInterpreter::f32_sqrt_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, float, Operators::SquareRoot>(configuration);
}
void BytecodeInterpreter::f32_add_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, float, Operators::Add>(configuration);
}
void BytecodeInterpreter::f32_sub_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, float, Operators::Subtract>(configuration);
}
void BytecodeInterpreter::f32_mul_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, float, Operators::Multiply>(configuration);
}
void BytecodeInterpreter::f32_div_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, float, Operators::Divide>(configuration);
}
void BytecodeInterpreter::f32_min_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, float, Operators::Minimum>(configuration);
}
void BytecodeInterpreter::f32_max_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, float, Operators::Maximum>(configuration);
}
void BytecodeInterpreter::f32_copysign_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<float, float, Operators::CopySign>(configuration);
}
void BytecodeInterpreter::f64_abs_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, double, Operators::Absolute>(configuration);
}
void BytecodeInterpreter::f64_neg_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, double, Operators::Negate>(configuration);
}
void BytecodeInterpreter::f64_ceil_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, double, Operators::Ceil>(configuration);
}
void BytecodeInterpreter::f64_floor_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, double, Operators::Floor>(configuration);
}
void BytecodeInterpreter::f64_trunc_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, double, Operators::Truncate>(configuration);
}
void BytecodeInterpreter::f64_nearest_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, double, Operators::Round>(configuration);
}
void BytecodeInterpreter::f64_sqrt_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, double, Operators::SquareRoot>(configuration);
}
void BytecodeInterpreter::f64_add_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, double, Operators::Add>(configuration);
}
void BytecodeInterpreter::f64_sub_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, double, Operators::Subtract>(configuration);
}
void BytecodeInterpreter::f64_mul_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, double, Operators::Multiply>(configuration);
}
void BytecodeInterpreter::f64_div_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, double, Operators::Divide>(configuration);
}
void BytecodeInterpreter::f64_min_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, double, Operators::Minimum>(configuration);
}
void BytecodeInterpreter::f64_max_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, double, Operators::Maximum>(configuration);
}
void BytecodeInterpreter::f64_copysign_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return binary_numeric_operation<double, double, Operators::CopySign>(configuration);
}
void BytecodeInterpreter::i32_wrap_i64_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i64, i32, Operators::Wrap<i32>>(configuration);
}
void BytecodeInterpreter::i32_trunc_sf32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, i32, Operators::CheckedTruncate<i32>>(configuration);
}
void BytecodeInterpreter::i32_trunc_uf32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, i32, Operators::CheckedTruncate<u32>>(configuration);
}
void BytecodeInterpreter::i32_trunc_sf64_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, i32, Operators::CheckedTruncate<i32>>(configuration);
}
void BytecodeInterpreter::i32_trunc_uf64_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, i32, Operators::CheckedTruncate<u32>>(configuration);
}
void BytecodeInterpreter::i64_trunc_sf32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, i64, Operators::CheckedTruncate<i64>>(configuration);
}
void BytecodeInterpreter::i64_trunc_uf32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, i64, Operators::CheckedTruncate<u64>>(configuration);
}
void BytecodeInterpreter::i64_trunc_sf64_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, i64, Operators::CheckedTruncate<i64>>(configuration);
}
void BytecodeInterpreter::i64_trunc_uf64_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, i64, Operators::CheckedTruncate<u64>>(configuration);
}
void BytecodeInterpreter::i64_extend_si32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i32, i64, Operators::Extend<i64>>(configuration);
}
void BytecodeInterpreter::i64_extend_ui32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<u32, i64, Operators::Extend<i64>>(configuration);
}
void BytecodeInterpreter::f32_convert_si32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i32, float, Operators::Convert<float>>(configuration);
}
void BytecodeInterpreter::f32_convert_ui32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<u32, float, Operators::Convert<float>>(configuration);
}
void BytecodeInterpreter::f32_convert_si64_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i64, float, Operators::Convert<float>>(configuration);
}
void BytecodeInterpreter::f32_convert_ui64_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<u64, float, Operators::Convert<float>>(configuration);
}
void BytecodeInterpreter::f32_demote_f64_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, float, Operators::Demote>(configuration);
}
void BytecodeInterpreter::f64_convert_si32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i32, double, Operators::Convert<double>>(configuration);
}
void BytecodeInterpreter::f64_convert_ui32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<u32, double, Operators::Convert<double>>(configuration);
}
void BytecodeInterpreter::f64_convert_si64_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i64, double, Operators::Convert<double>>(configuration);
}
void BytecodeInterpreter::f64_convert_ui64_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<u64, double, Operators::Convert<double>>(configuration);
}
void BytecodeInterpreter::f64_promote_f32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, double, Operators::Promote>(configuration);
}
void BytecodeInterpreter::i32_reinterpret_f32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, i32, Operators::Reinterpret<i32>>(configuration);
}
void BytecodeInterpreter::i64_reinterpret_f64_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, i64, Operators::Reinterpret<i64>>(configuration);
}
void BytecodeInterpreter::f32_reinterpret_i32_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i32, float, Operators::Reinterpret<float>>(configuration);
}
void BytecodeInterpreter::f64_reinterpret_i64_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i64, double, Operators::Reinterpret<double>>(configuration);
}
void BytecodeInterpreter::i32_extend8_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i32, i32, Operators::SignExtend<i8>>(configuration);
}
void BytecodeInterpreter::i32_extend16_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i32, i32, Operators::SignExtend<i16>>(configuration);
}
void BytecodeInterpreter::i64_extend8_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i64, i64, Operators::SignExtend<i8>>(configuration);
}
void BytecodeInterpreter::i64_extend16_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i64, i64, Operators::SignExtend<i16>>(configuration);
}
void BytecodeInterpreter::i64_extend32_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<i64, i64, Operators::SignExtend<i32>>(configuration);
}
void BytecodeInterpreter::i32_trunc_sat_f32_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, i32, Operators::SaturatingTruncate<i32>>(configuration);
}
void BytecodeInterpreter::i32_trunc_sat_f32_u_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, i32, Operators::SaturatingTruncate<u32>>(configuration);
}
void BytecodeInterpreter::i32_trunc_sat_f64_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, i32, Operators::SaturatingTruncate<i32>>(configuration);
}
void BytecodeInterpreter::i32_trunc_sat_f64_u_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, i32, Operators::SaturatingTruncate<u32>>(configuration);
}
void BytecodeInterpreter::i64_trunc_sat_f32_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, i64, Operators::SaturatingTruncate<i64>>(configuration);
}
void BytecodeInterpreter::i64_trunc_sat_f32_u_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<float, i64, Operators::SaturatingTruncate<u64>>(configuration);
}
void BytecodeInterpreter::i64_trunc_sat_f64_s_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, i64, Operators::SaturatingTruncate<i64>>(configuration);
}
void BytecodeInterpreter::i64_trunc_sat_f64_u_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unary_operation<double, i64, Operators::SaturatingTruncate<u64>>(configuration);
}

void BytecodeInterpreter::unimplemented_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    dbgln("Instruction '{}' not implemented", instruction_name(instruction.opcode()));
    m_trap = Trap { String::formatted("Unimplemented instruction {}", instruction_name(instruction.opcode())) };
}

void BytecodeInterpreter::memory_copy_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unimplemented_impl(configuration, ip, instruction);
}
void BytecodeInterpreter::memory_init_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unimplemented_impl(configuration, ip, instruction);
}
void BytecodeInterpreter::data_drop_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unimplemented_impl(configuration, ip, instruction);
}
void BytecodeInterpreter::elem_drop_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unimplemented_impl(configuration, ip, instruction);
}
void BytecodeInterpreter::table_copy_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unimplemented_impl(configuration, ip, instruction);
}
void BytecodeInterpreter::table_size_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unimplemented_impl(configuration, ip, instruction);
}
void BytecodeInterpreter::table_fill_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unimplemented_impl(configuration, ip, instruction);
}
void BytecodeInterpreter::table_init_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unimplemented_impl(configuration, ip, instruction);
}
void BytecodeInterpreter::memory_fill_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unimplemented_impl(configuration, ip, instruction);
}
void BytecodeInterpreter::table_grow_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unimplemented_impl(configuration, ip, instruction);
}
void BytecodeInterpreter::table_set_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unimplemented_impl(configuration, ip, instruction);
}
void BytecodeInterpreter::table_get_impl([[maybe_unused]] Configuration& configuration, [[maybe_unused]] InstructionPointer& ip, [[maybe_unused]] Instruction const& instruction)
{
    return unimplemented_impl(configuration, ip, instruction);
}
}
