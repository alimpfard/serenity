/*
 * Copyright (c) 2021, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibJS/Bytecode/Interpreter.h>
#include <LibJS/Runtime/ECMAScriptFunctionObject.h>
#include <LibJS/Runtime/Object.h>
#include <LibThreading/ConditionVariable.h>
#include <LibThreading/Mutex.h>
#include <LibThreading/MutexProtected.h>

namespace JS {

class GeneratorObject final : public Object {
    JS_OBJECT(GeneratorObject, Object);

public:
    static constexpr auto thread_stack_size = 4 * KiB;
    static ThrowCompletionOr<GeneratorObject*> create(Realm&, Value, ECMAScriptFunctionObject*, ExecutionContext, Bytecode::RegisterWindow);
    static ThrowCompletionOr<GeneratorObject*> create_ast(Realm&, ECMAScriptFunctionObject*, ExecutionContext);
    virtual void initialize(Realm&) override;
    virtual ~GeneratorObject() override = default;
    void visit_edges(Cell::Visitor&) override;

    ThrowCompletionOr<Value> next_impl(VM&, Optional<Value> next_argument, Optional<Value> value_to_throw);
    void set_done() { m_done = true; }

    ThrowCompletionOr<Value> yield(ThrowCompletionOr<Value> value);

    ECMAScriptFunctionObject const* function_object() const { return m_generating_function; }

private:
    GeneratorObject(Realm&, Object& prototype, ExecutionContext);
    static void* run_ast(void* self);

    ExecutionContext m_execution_context;
    ECMAScriptFunctionObject* m_generating_function { nullptr };

    class ThreadStack final : public JS::Cell {
        JS_CELL(ThreadStack, JS::Cell);

    public:
        ThreadStack() = default;
        virtual ~ThreadStack() override = default;
        virtual void visit_edges(Cell::Visitor&) override { }

        u8* base() { return m_stack; }
        size_t size() const { return thread_stack_size; }

    private:
        u8 m_stack[GeneratorObject::thread_stack_size];
    };

    struct BytecodeMode {
        Value previous_value;
        Bytecode::RegisterWindow frame;
    };
    struct ASTMode {
        pthread_t thread_id;
        pthread_attr_t thread_attributes;
        NonnullGCPtr<ThreadStack> thread_stack;
        Threading::MutexProtected<ThrowCompletionOr<Value>> return_value;
        Threading::MutexProtected<ThrowCompletionOr<Value>> next_value;
        Threading::Mutex exec_mutex;
        Threading::Mutex ready_mutex;
        Threading::Mutex value_mutex;
        Threading::ConditionVariable exec_condition;
        Threading::ConditionVariable ready_condition;
        Threading::ConditionVariable value_condition;
        bool dying { false };

        ASTMode(pthread_t thread_id, NonnullGCPtr<ThreadStack> stack)
            : thread_id(thread_id)
            , thread_attributes()
            , thread_stack(stack)
            , return_value(js_undefined())
            , next_value(js_undefined())
            , exec_mutex()
            , ready_mutex()
            , value_mutex()
            , exec_condition(exec_mutex)
            , ready_condition(ready_mutex)
            , value_condition(value_mutex)
        {
        }

        ~ASTMode()
        {
            dying = true;
            exec_condition.signal();
            // We always hold the value mutex, unlock it.
            value_mutex.unlock();
        }
    };
    Variant<Empty, BytecodeMode, NonnullOwnPtr<ASTMode>> m_impl;
    bool m_done { false };
};

}
