/*
 * Copyright (c) 2020-2022, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/HashTable.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibJS/Lexer.h>
#include <LibJS/SourceRange.h>

namespace JS {

class ScopePusher;

struct ParserError {
    String message;
    Optional<Position> position;

    String to_string() const
    {
        if (!position.has_value())
            return message;
        return String::formatted("{} (line: {}, column: {})", message, position.value().line, position.value().column);
    }

    String source_location_hint(StringView source, const char spacer = ' ', const char indicator = '^') const
    {
        if (!position.has_value())
            return {};
        // We need to modify the source to match what the lexer considers one line - normalizing
        // line terminators to \n is easier than splitting using all different LT characters.
        String source_string = source.replace("\r\n", "\n").replace("\r", "\n").replace(LINE_SEPARATOR_STRING, "\n").replace(PARAGRAPH_SEPARATOR_STRING, "\n");
        StringBuilder builder;
        builder.append(source_string.split_view('\n', true)[position.value().line - 1]);
        builder.append('\n');
        for (size_t i = 0; i < position.value().column - 1; ++i)
            builder.append(spacer);
        builder.append(indicator);
        return builder.build();
    }
};

struct ParserState {
    Lexer lexer;
    Token current_token;
    Vector<ParserError> errors;
    ScopePusher* current_scope_pusher { nullptr };

    HashMap<StringView, Optional<Position>> labels_in_scope;
    HashTable<StringView>* referenced_private_names { nullptr };

    bool strict_mode { false };
    bool allow_super_property_lookup { false };
    bool allow_super_constructor_call { false };
    bool in_function_context { false };
    bool in_formal_parameter_context { false };
    bool in_generator_function_context { false };
    bool await_expression_is_valid { false };
    bool in_arrow_function_context { false };
    bool in_break_context { false };
    bool in_continue_context { false };
    bool string_legacy_octal_escape_sequence_in_scope { false };
    bool in_class_field_initializer { false };
    bool in_class_static_init_block { false };
    bool function_might_need_arguments_object { false };

    ParserState(Lexer, bool should_allow_html_comments);
};

enum class Associativity {
    Left,
    Right
};

// Note: Keep in sync with the parameters of Parser::parse_expression().
struct ExpressionParseData {
    int min_precedence;
    Associativity associate;
    Vector<TokenType> forbidden;
};

}
