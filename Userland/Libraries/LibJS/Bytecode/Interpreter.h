/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Generator.h"
#include "PassManager.h"
#include <LibJS/Bytecode/Label.h>
#include <LibJS/Bytecode/Register.h>
#include <LibJS/Forward.h>
#include <LibJS/Heap/Cell.h>
#include <LibJS/Heap/Handle.h>
#include <LibJS/Runtime/VM.h>
#include <LibJS/Runtime/Value.h>

namespace JS::Bytecode {

struct RegisterWindow {
    MarkedVector<Value> registers;
    MarkedVector<Environment*> saved_lexical_environments;
    MarkedVector<Environment*> saved_variable_environments;
};

class Interpreter {
public:
    Interpreter(GlobalObject&, Realm&);
    ~Interpreter();

    // FIXME: Remove this thing once we don't need it anymore!
    static Interpreter* current();

    GlobalObject& global_object() { return m_global_object; }
    Realm& realm() { return m_realm; }
    VM& vm() { return m_vm; }

    void enter_script_or_module(ScriptOrModule script_or_module)
    {
        m_script_or_module = script_or_module;
    }
    void leave_script_or_module()
    {
        m_script_or_module = {};
    }

    ThrowCompletionOr<Value> run(Bytecode::Executable const& executable, Bytecode::BasicBlock const* entry_point = nullptr)
    {
        auto value_and_frame = run_and_return_frame(executable, entry_point);
        return move(value_and_frame.value);
    }

    struct ValueAndFrame {
        ThrowCompletionOr<Value> value;
        OwnPtr<RegisterWindow> frame;
    };
    ValueAndFrame run_and_return_frame(Bytecode::Executable const&, Bytecode::BasicBlock const* entry_point);

    ALWAYS_INLINE Value& accumulator() { return reg(Register::accumulator()); }
    Value& reg(Register const& r) { return registers()[r.index()]; }
    [[nodiscard]] RegisterWindow snapshot_frame() const { return m_register_windows.last(); }

    auto& saved_lexical_environment_stack() { return m_register_windows.last().saved_lexical_environments; }
    auto& saved_variable_environment_stack() { return m_register_windows.last().saved_variable_environments; }

    void enter_frame(RegisterWindow const& frame)
    {
        m_manually_entered_frames.append(true);
        m_register_windows.append(make<RegisterWindow>(frame));
    }
    NonnullOwnPtr<RegisterWindow> pop_frame()
    {
        VERIFY(!m_manually_entered_frames.is_empty());
        VERIFY(m_manually_entered_frames.last());
        m_manually_entered_frames.take_last();
        return m_register_windows.take_last();
    }

    void jump(Label const& label)
    {
        m_pending_jump = &label.block();
    }
    void do_return(Value return_value) { m_return_value = return_value; }

    void enter_unwind_context(Optional<Label> handler_target, Optional<Label> finalizer_target);
    void leave_unwind_context();
    ThrowCompletionOr<void> continue_pending_unwind(Label const& resume_label);

    Executable const& current_executable() { return *m_current_executable; }

    enum class OptimizationLevel {
        Default,
        __Count,
    };
    static Bytecode::PassManager& optimization_pipeline(OptimizationLevel = OptimizationLevel::Default);

    VM::InterpreterExecutionScope ast_interpreter_scope();

private:
    MarkedVector<Value>& registers() { return m_register_windows.last().registers; }

    static AK::Array<OwnPtr<PassManager>, static_cast<UnderlyingType<Interpreter::OptimizationLevel>>(Interpreter::OptimizationLevel::__Count)> s_optimization_pipelines;

    VM& m_vm;
    GlobalObject& m_global_object;
    Realm& m_realm;
    NonnullOwnPtrVector<RegisterWindow> m_register_windows;
    Vector<bool> m_manually_entered_frames;
    Optional<BasicBlock const*> m_pending_jump;
    Value m_return_value;
    Executable const* m_current_executable { nullptr };
    Vector<UnwindInfo> m_unwind_contexts;
    Handle<Value> m_saved_exception;
    OwnPtr<JS::Interpreter> m_ast_interpreter;
    ScriptOrModule m_script_or_module {};
};

extern bool g_dump_bytecode;

}
