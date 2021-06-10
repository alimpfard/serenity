/*
 * Copyright (c) 2021, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/TemporaryChange.h>
#include <LibJS/Bytecode/Generator.h>
#include <LibJS/Bytecode/Interpreter.h>
#include <LibJS/Runtime/GeneratorObject.h>
#include <LibJS/Runtime/GlobalObject.h>

namespace JS {

GeneratorObject* GeneratorObject::create(GlobalObject& global_object, Value initial_value, ScriptFunction* generating_function, ScopeObject* generating_scope)
{
    auto object = global_object.heap().allocate<GeneratorObject>(global_object, global_object);
    object->m_previous_value = initial_value;
    object->m_generating_function = generating_function;
    object->m_scope = generating_scope;
    return object;
}

GeneratorObject::GeneratorObject(GlobalObject& global_object)
    : Object(*global_object.object_prototype())
{
}

void GeneratorObject::initialize(GlobalObject& global_object)
{
    auto& vm = this->vm();
    Object::initialize(global_object);
    define_native_function(vm.names.next, next);
    define_native_function(vm.names.return_, return_);
    define_native_function(vm.names.throw_, throw_);
}

GeneratorObject::~GeneratorObject()
{
}

void GeneratorObject::visit_edges(Cell::Visitor& visitor)
{
    Object::visit_edges(visitor);
    visitor.visit(m_scope);
    visitor.visit(m_generating_function);
    if (m_previous_value.is_object())
        visitor.visit(&m_previous_value.as_object());
}

GeneratorObject* GeneratorObject::typed_this(VM& vm, GlobalObject& global_object)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    asm volatile("int3");
    if (!this_object)
        return {};
    if (!is<GeneratorObject>(this_object)) {
        vm.throw_exception<TypeError>(global_object, ErrorType::NotA, "Generator");
        return nullptr;
    }
    return static_cast<GeneratorObject*>(this_object);
}

Value GeneratorObject::next_impl(VM& vm, GlobalObject& global_object, Optional<Value> value_to_throw)
{
    asm volatile("int3");

    auto bytecode_interpreter = Bytecode::Interpreter::current();
    VERIFY(bytecode_interpreter);

    Value previous_generated_value { js_undefined() };
    if (m_previous_value.is_object())
        previous_generated_value = m_previous_value.as_object().get("result");

    if (vm.exception())
        return {};

    auto result = Object::create_empty(global_object);
    result->put("value", move(previous_generated_value));

    if (m_done) {
        result->put("done", Value(true));
        return result;
    }

    // Extract the continuation
    Bytecode::BasicBlock const* next_block { nullptr };
    if (m_previous_value.is_object())
        next_block = reinterpret_cast<Bytecode::BasicBlock const*>(static_cast<u64>(m_previous_value.as_object().get("continuation").to_double(global_object)));

    if (!next_block) {
        // The generator has terminated, now we can simply return done=true.
        m_done = true;
        result->put("done", Value(true));
        return result;
    }

    // Make sure it's an actual block
    VERIFY(!m_generating_function->bytecode_executable()->basic_blocks.find_if([next_block](auto& block) { return block == next_block; }).is_end());

    // Pretend that 'yield' returned the passed value
    if (value_to_throw.has_value()) {
        vm.throw_exception(global_object, *value_to_throw);
        bytecode_interpreter->accumulator() = js_undefined();
    } else {
        bytecode_interpreter->accumulator() = vm.argument(0);
    }

    // Temporarily switch to the captured scope
    TemporaryChange change { vm.call_frame().scope, m_scope };

    m_previous_value = bytecode_interpreter->run(*m_generating_function->bytecode_executable(), next_block);
    result->put("done", Value(false));
    return result;
}

JS_DEFINE_NATIVE_FUNCTION(GeneratorObject::next)
{
    auto object = typed_this(vm, global_object);
    if (!object)
        return {};
    return object->next_impl(vm, global_object, {});
}

JS_DEFINE_NATIVE_FUNCTION(GeneratorObject::return_)
{
    auto object = typed_this(vm, global_object);
    if (!object)
        return {};
    object->m_done = true;
    return object->next_impl(vm, global_object, {});
}

JS_DEFINE_NATIVE_FUNCTION(GeneratorObject::throw_)
{
    auto object = typed_this(vm, global_object);
    if (!object)
        return {};
    return object->next_impl(vm, global_object, vm.argument(0));
}

}
