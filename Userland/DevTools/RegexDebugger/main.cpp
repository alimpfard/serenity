/*
 * Copyright (c) 2021, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <DevTools/RegexDebugger/MainWindowGML.h>
#include <LibGUI/Application.h>
#include <LibGUI/Button.h>
#include <LibGUI/Slider.h>
#include <LibGUI/SpinBox.h>
#include <LibGUI/TextBox.h>
#include <LibGUI/TextEditor.h>
#include <LibGUI/Window.h>
#include <LibRegex/Regex.h>
#include <unistd.h>

enum class JumpDestination {
    First,
    Next,
    Previous,
    Last,
};

struct Trace {
    size_t source_offset { 0 };
    size_t subject_code_point_offset { 0 };
    size_t bytecode_ip { 0 };
    String result;
};

template<typename RegexT>
struct Debugger : public regex::Debugger {

    explicit Debugger(regex::Regex<RegexT> const& regex)
        : m_regex(regex)
        , m_trace({})
    {
    }
    ~Debugger() = default;

    virtual void leave_opcode(regex::OpCode const&, regex::ByteCode const&, regex::MatchInput const&, regex::MatchState const& state, regex::ExecutionResult result)
    {
        auto source_offset_ptr = const_cast<regex::DebugInfo&>(m_regex.debug_information()).line_info.find_largest_not_above(state.instruction_position);
        if (!source_offset_ptr) {
            dbgln("Failed to find the source offset for IP {}", state.instruction_position);
            return;
        }
        m_trace.empend(*source_offset_ptr, state.string_position_in_code_units, state.instruction_position, regex::execution_result_name(result));
    }
    auto& trace() { return m_trace; }

private:
    regex::Regex<RegexT> const& m_regex;
    Vector<Trace> m_trace {};
};

int main(int argc, char** argv)
{
    if (pledge("stdio thread recvfd sendfd cpath rpath wpath unix", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    auto app = GUI::Application::construct(argc, argv);

    if (pledge("stdio thread recvfd sendfd rpath cpath wpath", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    auto window = GUI::Window::construct();
    auto& main_widget = window->set_main_widget<GUI::Widget>();
    main_widget.load_from_gml(main_window_gml);

    auto& regex_input = *main_widget.find_descendant_of_type_named<GUI::TextBox>("regex_input"sv);
    auto& subject_text_input = *main_widget.find_descendant_of_type_named<GUI::TextEditor>("text_input"sv);
    auto& step_spinbox = *main_widget.find_descendant_of_type_named<GUI::SpinBox>("step_spinbox"sv);
    auto& step_slider = *main_widget.find_descendant_of_type_named<GUI::Slider>("step_slider"sv);
    auto& first_button = *main_widget.find_descendant_of_type_named<GUI::Button>("jump_to_first_button"sv);
    auto& next_button = *main_widget.find_descendant_of_type_named<GUI::Button>("jump_to_next_button"sv);
    auto& previous_button = *main_widget.find_descendant_of_type_named<GUI::Button>("jump_to_previous_button"sv);
    auto& last_button = *main_widget.find_descendant_of_type_named<GUI::Button>("jump_to_last_button"sv);
    auto& bytecode_viewer = *main_widget.find_descendant_of_type_named<GUI::TextEditor>("bytecode_viewer"sv);

    bytecode_viewer.set_gutter_visible(true);

    regex::Regex<ECMA262> regex { "(?:)" };
    Vector<Trace> trace;
    HashMap<size_t, size_t> ip_to_line_map;
    size_t current_step { 0 };

    auto jump_to_specific = [&](auto step) {
        if (trace.is_empty())
            current_step = 0;
        else
            current_step = clamp(step, 0, trace.size() - 1);

        next_button.set_enabled(true);
        previous_button.set_enabled(true);

        if (current_step + 1 == trace.size())
            next_button.set_enabled(false);

        if (current_step == 0)
            previous_button.set_enabled(false);

        step_slider.set_value(current_step);
        step_spinbox.set_value(current_step);

        if (trace.is_empty())
            return;

        auto& trace_entry = trace[current_step];

        bytecode_viewer.set_cursor_and_focus_line(ip_to_line_map.get(trace_entry.bytecode_ip).value(), 0);
        bytecode_viewer.select_current_line();
        regex_input.set_selection({
            { 0, trace_entry.source_offset },
            { 0, trace_entry.source_offset + 1 },
        });
        // FIXME: This, but for multiline.
        subject_text_input.set_selection({
            { 0, trace_entry.subject_code_point_offset - 1 },
            { 0, trace_entry.subject_code_point_offset },
        });
    };
    auto jump_to = [&](JumpDestination destination) {
        switch (destination) {
        case JumpDestination::First:
            current_step = 0;
            break;
        case JumpDestination::Next:
            current_step++;
            break;
        case JumpDestination::Previous:
            current_step--;
            break;
        case JumpDestination::Last:
            current_step = trace.size() - 1;
            break;
        }
        jump_to_specific(current_step);
    };
    auto recompile_regex = [&](auto pattern) {
        bytecode_viewer.clear();
        ip_to_line_map.clear();
        regex = regex::Regex<ECMA262>(pattern, regex::ECMAScriptFlags::EmitDebugInfo); // FIXME: Flags
        if (regex.parser_result.error == regex::Error::NoError) {
            regex_input.set_tooltip(""sv);
            regex::MatchState state;
            auto& bytecode = regex.parser_result.bytecode;
            StringBuilder builder;
            size_t index = 0;

            for (;;) {
                ip_to_line_map.set(state.instruction_position, index++);
                auto& opcode = bytecode.get_opcode(state);
                if (is<regex::OpCode_Compare>(opcode)) {
                    builder.appendff("{:04}: {} ", state.instruction_position, opcode.name());
                    bool first = true;
                    for (auto& line : to<regex::OpCode_Compare>(opcode).variable_arguments_to_string()) {
                        if (first)
                            first = false;
                        else
                            builder.append(", ");
                        builder.append("{");
                        builder.append(line);
                        builder.append("}");
                    }
                    builder.append('\n');
                } else {
                    builder.appendff("{:04}: {}", state.instruction_position, opcode.name());
                    if (opcode.size() > 1)
                        builder.appendff(" {}", opcode.arguments_string());
                    builder.append('\n');
                }
                if (is<regex::OpCode_Exit>(opcode))
                    break;
                state.instruction_position += opcode.size();
            }

            bytecode_viewer.set_text(builder.string_view());
        } else {
            regex_input.set_tooltip(regex.error_string());
        }
    };

    auto recalculate_steps = [&](auto subject) {
        if (regex.parser_result.error != regex::Error::NoError)
            return;

        Debugger debugger { regex };
        regex.attach_debugger(debugger);
        // FIXME: Get results
        regex.match(subject);
        regex.detach_debugger();

        trace = move(debugger.trace());

        if (trace.size() > 0) {
            step_spinbox.set_range(0, trace.size() - 1);
            step_slider.set_range(0, trace.size() - 1);
            step_spinbox.set_value(0);
            step_slider.set_value(0);
            step_spinbox.set_enabled(true);
            step_slider.set_enabled(true);
        } else {
            step_slider.set_range(0, 0);
            step_spinbox.set_range(0, 0);
            step_spinbox.set_enabled(false);
            step_slider.set_enabled(false);
        }

        jump_to_specific(0);
    };

    first_button.on_click = [&](auto) { jump_to(JumpDestination::First); };
    next_button.on_click = [&](auto) { jump_to(JumpDestination::Next); };
    previous_button.on_click = [&](auto) { jump_to(JumpDestination::Previous); };
    last_button.on_click = [&](auto) { jump_to(JumpDestination::Last); };

    step_slider.on_change = [&](auto step) { jump_to_specific(step); };
    step_spinbox.on_change = [&](auto step) { jump_to_specific(step); };

    subject_text_input.on_change = [&] {
        jump_to_specific(0);
        recalculate_steps(subject_text_input.text());
    };
    regex_input.on_change = [&] {
        jump_to_specific(0);
        recompile_regex(regex_input.text());
        recalculate_steps(subject_text_input.text());
    };

    jump_to_specific(0);

    window->show();
    return app->exec();
}
