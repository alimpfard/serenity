/*
 * Copyright (c) 2021, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/TemporaryChange.h>
#include <LibCore/EventLoop.h>
#include <LibJS/Bytecode/Generator.h>
#include <LibJS/Bytecode/Interpreter.h>
#include <LibJS/Heap/Cell.h>
#include <LibJS/Heap/DeferGC.h>
#include <LibJS/Runtime/GeneratorObject.h>
#include <LibJS/Runtime/GeneratorPrototype.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <pthread.h>
#include <signal.h>

namespace JS {

ThrowCompletionOr<GeneratorObject*> GeneratorObject::create(Realm& realm, Value initial_value, ECMAScriptFunctionObject* generating_function, ExecutionContext execution_context, Bytecode::RegisterWindow frame)
{
    auto& vm = realm.vm();
    // This is "g1.prototype" in figure-2 (https://tc39.es/ecma262/img/figure-2.png)
    Value generating_function_prototype;
    if (generating_function->kind() == FunctionKind::Async) {
        // We implement async functions by transforming them to generator function in the bytecode
        // interpreter. However an async function does not have a prototype and should not be
        // changed thus we hardcode the prototype.
        generating_function_prototype = realm.intrinsics().generator_prototype();
    } else {
        generating_function_prototype = TRY(generating_function->get(vm.names.prototype));
    }
    auto* generating_function_prototype_object = TRY(generating_function_prototype.to_object(vm));
    auto object = realm.heap().allocate<GeneratorObject>(realm, realm, *generating_function_prototype_object, move(execution_context));
    object->m_generating_function = generating_function;
    object->m_impl = BytecodeMode { initial_value, move(frame) };
    return object;
}

void* GeneratorObject::run_ast(void* _self)
{
    Core::EventLoop thread_loop;

    dbgln_if(false, "Run AST - start");
    auto& self = *static_cast<GeneratorObject*>(_self);
    auto& data = self.m_impl.get<NonnullOwnPtr<ASTMode>>();

    dbgln_if(false, "Run AST - Locking exec");
    data->exec_mutex.lock();

    dbgln_if(false, "Run AST - Locked exec, unblocking create()");
    data->ready_mutex.lock();
    data->ready_condition.signal();
    data->ready_mutex.unlock();

    dbgln_if(false, "Run AST - Locked exec, waiting for signal");
    data->exec_condition.wait();
    data->exec_mutex.unlock();

    if (data->dying) {
        dbgln_if(false, "Run AST - Dying");
        // Bye bye
        return nullptr;
    }
    dbgln_if(false, "Run AST - Signal received, locking exec");
    data->exec_mutex.lock();
    dbgln_if(false, "Run AST - Locked exec, running AST");
    auto result = self.m_generating_function->call_in_context(self.m_execution_context, self.m_execution_context.this_value);
    data->return_value.with_locked([&](auto& return_value) { return_value = move(result); });
    self.m_done = true;
    dbgln_if(false, "Run AST - AST done, unlocking exec");
    data->exec_mutex.unlock();

    dbgln_if(false, "Run AST - end, signaling value");
    data->value_mutex.lock();
    data->value_condition.signal();
    data->value_mutex.unlock();

    dbgln_if(false, "Run AST - end");
    return nullptr;
}

ThrowCompletionOr<GeneratorObject*> GeneratorObject::create_ast(Realm& realm, ECMAScriptFunctionObject* generating_function, ExecutionContext execution_context)
{
    // This is "g1.prototype" in figure-2 (https://tc39.es/ecma262/img/figure-2.png)
    Value generating_function_prototype;
    if (generating_function->kind() == FunctionKind::Async) {
        // We implement async functions by transforming them to generator function in the bytecode
        // interpreter. However an async function does not have a prototype and should not be
        // changed thus we hardcode the prototype.
        generating_function_prototype = realm.intrinsics().generator_prototype();
    } else {
        generating_function_prototype = TRY(generating_function->get(realm.vm().names.prototype));
    }
    auto* generating_function_prototype_object = TRY(generating_function_prototype.to_object(realm.vm()));
    if (execution_context.lexical_environment->has_this_binding()) {
        execution_context.this_value = TRY(execution_context.lexical_environment->get_this_binding(realm.vm()));
    } else {
        execution_context.this_value = js_undefined();
    }
    auto object = realm.heap().allocate<GeneratorObject>(realm, realm, *generating_function_prototype_object, move(execution_context));
    object->m_execution_context.generator_function = object;
    object->m_generating_function = generating_function;
    auto stack = NonnullGCPtr(*realm.heap().allocate<ThreadStack>(realm));
    object->m_impl = make<ASTMode>(0, stack);
    auto& impl = object->m_impl.get<NonnullOwnPtr<ASTMode>>();
    impl->exec_mutex.lock();
    impl->value_mutex.lock();
    pthread_attr_setstack(&impl->thread_attributes, stack->base(), stack->size());
    pthread_create(&impl->thread_id, nullptr, &GeneratorObject::run_ast, object);
    impl->exec_mutex.unlock();

    impl->ready_mutex.lock();
    impl->ready_condition.wait();
    impl->ready_mutex.unlock();

    return object;
}

GeneratorObject::GeneratorObject(Realm&, Object& prototype, ExecutionContext context)
    : Object(prototype)
    , m_execution_context(move(context))
{
}

void GeneratorObject::initialize(Realm&)
{
}

void GeneratorObject::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_generating_function);
    m_impl.visit(
        [&](BytecodeMode const& p) { visitor.visit(p.previous_value); },
        [&](NonnullOwnPtr<ASTMode> const& p) { visitor.visit(p->thread_stack); },
        [](Empty) {});
}

ThrowCompletionOr<Value> GeneratorObject::yield(ThrowCompletionOr<Value> value)
{
    dbgln_if(false, "Yield!");
    auto& data = m_impl.get<NonnullOwnPtr<ASTMode>>();
    data->return_value.with_locked([&](auto& return_value) { return_value = move(value); });
    dbgln_if(false, "Yield - Signaling value ready");
    data->value_mutex.lock();
    data->value_condition.signal();
    data->value_mutex.unlock();

    vm().pop_execution_context();
    dbgln_if(false, "Yield - Waiting for execution");
    data->exec_condition.wait();
    data->exec_mutex.unlock();

    if (data->dying) {
        dbgln_if(false, "Yield - Dying");
        // Bye bye
        pthread_exit(nullptr);
    }
    dbgln_if(false, "Yield - Locking exec");
    data->exec_mutex.lock();
    dbgln_if(false, "Yield - Restoring execution state");
    vm().push_execution_context(m_execution_context);
    dbgln_if(false, "Yield - Returning");
    return data->next_value.with_locked([](auto& next_value) { return move(next_value); });
}

ThrowCompletionOr<Value> GeneratorObject::next_impl(VM& vm, Optional<Value> next_argument, Optional<Value> value_to_throw)
{
    auto& realm = *vm.current_realm();
    if (m_done) {
        auto result = Object::create(realm, realm.intrinsics().object_prototype());
        result->define_direct_property("value", js_undefined(), default_attributes);
        result->define_direct_property("done", Value(true), default_attributes);
        return result;
    }

    dbgln_if(false, "Next!");
    return m_impl.visit(
        [&](BytecodeMode& data) -> ThrowCompletionOr<Value> {
            auto bytecode_interpreter = Bytecode::Interpreter::current();
            VERIFY(bytecode_interpreter);

            auto generated_value = [](Value value) -> ThrowCompletionOr<Value> {
                if (value.is_object())
                    return TRY(value.as_object().get("result"));
                return value.is_empty() ? js_undefined() : value;
            };

            auto generated_continuation = [&](Value value) -> ThrowCompletionOr<Bytecode::BasicBlock const*> {
                if (value.is_object()) {
                    auto number_value = TRY(value.as_object().get("continuation"));
                    return reinterpret_cast<Bytecode::BasicBlock const*>(static_cast<u64>(TRY(number_value.to_double(realm.vm()))));
                }
                return nullptr;
            };

            auto previous_generated_value = TRY(generated_value(data.previous_value));

            auto result = Object::create(realm, realm.intrinsics().object_prototype());
            result->define_direct_property("value", previous_generated_value, default_attributes);

            if (m_done) {
                result->define_direct_property("done", Value(true), default_attributes);
                return result;
            }

            // Extract the continuation
            auto next_block = TRY(generated_continuation(data.previous_value));

            if (!next_block) {
                // The generator has terminated, now we can simply return done=true.
                m_done = true;
                result->define_direct_property("done", Value(true), default_attributes);
                return result;
            }

            // Make sure it's an actual block
            VERIFY(!m_generating_function->bytecode_executable()->basic_blocks.find_if([next_block](auto& block) { return block == next_block; }).is_end());

            // Temporarily switch to the captured execution context
            vm.push_execution_context(m_execution_context);

            // Pretend that 'yield' returned the passed value, or threw
            if (value_to_throw.has_value()) {
                bytecode_interpreter->accumulator() = js_undefined();
                return throw_completion(value_to_throw.release_value());
            }

            Bytecode::RegisterWindow* frame = nullptr;
            if (auto* p = m_impl.get_pointer<BytecodeMode>())
                frame = &p->frame;

            auto next_value = next_argument.value_or(js_undefined());
            if (frame)
                frame->registers[0] = next_value;
            else
                bytecode_interpreter->accumulator() = next_value;

            auto next_result = bytecode_interpreter->run_and_return_frame(*m_generating_function->bytecode_executable(), next_block, frame);

            vm.pop_execution_context();

            data.previous_value = TRY(move(next_result.value));
            m_done = TRY(generated_continuation(data.previous_value)) == nullptr;

            result->define_direct_property("value", TRY(generated_value(data.previous_value)), default_attributes);
            result->define_direct_property("done", Value(m_done), default_attributes);

            return result;
        },
        [&](NonnullOwnPtr<ASTMode>& data) -> ThrowCompletionOr<Value> {
            dbgln_if(false, "Next - AST");
            Optional<ThrowCompletionOr<Value>> next_value;
            if (next_argument.has_value())
                next_value = next_argument.release_value();
            else if (value_to_throw.has_value())
                next_value = JS::throw_completion(value_to_throw.release_value());
            else
                next_value = js_undefined();

            data->next_value.with_locked([&](auto& value) { value = next_value.release_value(); });

            dbgln_if(false, "Next - AST: Signal exec");
            data->exec_mutex.lock();
            data->exec_condition.signal();
            data->exec_mutex.unlock();

            dbgln_if(false, "Next - AST: Waiting for value");
            data->value_condition.wait();
            data->value_mutex.unlock();

            dbgln_if(false, "Next - AST: Received value");
            auto result = data->return_value.with_locked([](auto& return_value) { return move(return_value); });
            dbgln_if(false, "Next - AST: Locking value");
            data->value_mutex.lock();
            dbgln_if(false, "Next - AST: Done (result is throw? {})", result.is_error());

            if (result.is_error())
                return result;

            auto result_object = Object::create(realm, realm.intrinsics().object_prototype());
            result_object->define_direct_property("value", result.value(), default_attributes);
            result_object->define_direct_property("done", Value(m_done), default_attributes);
            return result_object;
        },
        [](Empty) -> ThrowCompletionOr<Value> { VERIFY_NOT_REACHED(); });
}

}
