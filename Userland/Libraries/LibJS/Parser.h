/*
 * Copyright (c) 2020, Stephan Unverwerth <s.unverwerth@serenityos.org>
 * Copyright (c) 2021-2022, David Tuin <davidot@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashTable.h>
#include <AK/StackInfo.h>
#include <AK/StringBuilder.h>
#include <LibJS/AST.h>
#include <LibJS/Lexer.h>
#include <LibJS/ParserState.h>
#include <LibJS/Runtime/FunctionConstructor.h>
#include <LibJS/SourceRange.h>
#include <stdio.h>

namespace JS {

struct FunctionNodeParseOptions {
    enum {
        CheckForFunctionAndName = 1 << 0,
        AllowSuperPropertyLookup = 1 << 1,
        AllowSuperConstructorCall = 1 << 2,
        IsGetterFunction = 1 << 3,
        IsSetterFunction = 1 << 4,
        IsArrowFunction = 1 << 5,
        IsGeneratorFunction = 1 << 6,
        IsAsyncFunction = 1 << 7,
    };
};

class ScopePusher;

class Parser {
public:
    explicit Parser(Lexer lexer, Program::Type program_type = Program::Type::Script);

    NonnullNodePtr<Program> parse_program(bool starts_in_strict_mode = false);

    template<typename FunctionNodeType>
    NonnullNodePtr<FunctionNodeType> parse_function_node(u8 parse_options = FunctionNodeParseOptions::CheckForFunctionAndName, Optional<Position> const& function_start = {});
    Vector<FunctionNode::Parameter> parse_formal_parameters(int& function_length, u8 parse_options = 0);

    enum class AllowDuplicates {
        Yes,
        No
    };

    enum class AllowMemberExpressions {
        Yes,
        No
    };

    NodePtr<BindingPattern> parse_binding_pattern(AllowDuplicates is_var_declaration = AllowDuplicates::No, AllowMemberExpressions allow_member_expressions = AllowMemberExpressions::No);

    struct PrimaryExpressionParseResult {
        NonnullNodePtr<Expression> result;
        bool should_continue_parsing_as_expression { true };
    };

    NonnullNodePtr<Declaration> parse_declaration();

    enum class AllowLabelledFunction {
        No,
        Yes
    };

    NonnullNodePtr<Statement> parse_statement(AllowLabelledFunction allow_labelled_function = AllowLabelledFunction::No);
    NonnullNodePtr<BlockStatement> parse_block_statement();
    NonnullNodePtr<FunctionBody> parse_function_body(Vector<FunctionDeclaration::Parameter> const& parameters, FunctionKind function_kind, bool& contains_direct_call_to_eval);
    NonnullNodePtr<ReturnStatement> parse_return_statement();
    NonnullNodePtr<VariableDeclaration> parse_variable_declaration(bool for_loop_variable_declaration = false);
    NonnullNodePtr<Statement> parse_for_statement();

    enum class IsForAwaitLoop {
        No,
        Yes
    };

    NonnullNodePtr<Statement> parse_for_in_of_statement(NonnullNodePtr<ASTNode> lhs, IsForAwaitLoop is_await);
    NonnullNodePtr<IfStatement> parse_if_statement();
    NonnullNodePtr<ThrowStatement> parse_throw_statement();
    NonnullNodePtr<TryStatement> parse_try_statement();
    NonnullNodePtr<CatchClause> parse_catch_clause();
    NonnullNodePtr<SwitchStatement> parse_switch_statement();
    NonnullNodePtr<SwitchCase> parse_switch_case();
    NonnullNodePtr<BreakStatement> parse_break_statement();
    NonnullNodePtr<ContinueStatement> parse_continue_statement();
    NonnullNodePtr<DoWhileStatement> parse_do_while_statement();
    NonnullNodePtr<WhileStatement> parse_while_statement();
    NonnullNodePtr<WithStatement> parse_with_statement();
    NonnullNodePtr<DebuggerStatement> parse_debugger_statement();
    NonnullNodePtr<ConditionalExpression> parse_conditional_expression(NonnullNodePtr<Expression> test);
    NonnullNodePtr<OptionalChain> parse_optional_chain(NonnullNodePtr<Expression> base);
    NonnullNodePtr<Expression> parse_expression(int min_precedence, Associativity associate = Associativity::Right, const Vector<TokenType>& forbidden = {});
    PrimaryExpressionParseResult parse_primary_expression();
    NonnullNodePtr<Expression> parse_unary_prefixed_expression();
    NonnullNodePtr<RegExpLiteral> parse_regexp_literal();
    NonnullNodePtr<ObjectExpression> parse_object_expression();
    NonnullNodePtr<ArrayExpression> parse_array_expression();
    NonnullNodePtr<StringLiteral> parse_string_literal(const Token& token, bool in_template_literal = false);
    NonnullNodePtr<TemplateLiteral> parse_template_literal(bool is_tagged);
    NonnullNodePtr<Expression> parse_secondary_expression(NonnullNodePtr<Expression>, int min_precedence, Associativity associate = Associativity::Right);
    NonnullNodePtr<Expression> parse_call_expression(NonnullNodePtr<Expression>);
    NonnullNodePtr<NewExpression> parse_new_expression();
    NonnullNodePtr<ClassDeclaration> parse_class_declaration();
    NonnullNodePtr<ClassExpression> parse_class_expression(bool expect_class_name);
    NonnullNodePtr<YieldExpression> parse_yield_expression();
    NonnullNodePtr<AwaitExpression> parse_await_expression();
    NonnullNodePtr<Expression> parse_property_key();
    NonnullNodePtr<AssignmentExpression> parse_assignment_expression(AssignmentOp, NonnullNodePtr<Expression> lhs, int min_precedence, Associativity);
    NonnullNodePtr<Identifier> parse_identifier();
    NonnullNodePtr<ImportStatement> parse_import_statement(Program& program);
    NonnullNodePtr<ExportStatement> parse_export_statement(Program& program);

    NodePtr<FunctionExpression> try_parse_arrow_function_expression(bool expect_parens, bool is_async = false);
    NodePtr<LabelledStatement> try_parse_labelled_statement(AllowLabelledFunction allow_function);
    NodePtr<MetaProperty> try_parse_new_target_expression();
    NodePtr<MetaProperty> try_parse_import_meta_expression();
    NonnullNodePtr<ImportCall> parse_import_call();

    Vector<CallExpression::Argument> parse_arguments();

    bool has_errors() const { return m_state.errors.size(); }
    const Vector<ParserError>& errors() const { return m_state.errors; }
    void print_errors(bool print_hint = true) const
    {
        for (auto& error : m_state.errors) {
            if (print_hint) {
                auto hint = error.source_location_hint(m_state.lexer.source());
                if (!hint.is_empty())
                    warnln("{}", hint);
            }
            warnln("SyntaxError: {}", error.to_string());
        }
    }

    struct TokenMemoization {
        bool try_parse_arrow_function_expression_failed;
    };

    // Needs to mess with m_state, and we're not going to expose a non-const getter for that :^)
    friend ThrowCompletionOr<ECMAScriptFunctionObject*> FunctionConstructor::create_dynamic_function(GlobalObject&, FunctionObject&, FunctionObject*, FunctionKind, MarkedValueList const&);

    void switch_to_state(ParserState state, Badge<ParseThunk>)
    {
        save_state();
        m_state = move(state);
    }
    void leave_state(Badge<ParseThunk>)
    {
        load_state();
    }
    void append_errors(Vector<ParserError> new_errors)
    {
        m_state.errors.extend(move(new_errors));
    }

private:
    friend class ScopePusher;

    void parse_script(Program& program, bool starts_in_strict_mode);
    void parse_module(Program& program);

    Associativity operator_associativity(TokenType) const;
    bool match_expression() const;
    bool match_unary_prefixed_expression() const;
    bool match_secondary_expression(const Vector<TokenType>& forbidden = {}) const;
    bool match_statement() const;
    bool match_export_or_import() const;
    bool match_assert_clause() const;
    bool match_declaration() const;
    bool try_match_let_declaration() const;
    bool match_variable_declaration() const;
    bool match_identifier() const;
    bool match_identifier_name() const;
    bool match_property_key() const;
    bool is_private_identifier_valid() const;
    bool match(TokenType type) const;
    bool done() const;
    void expected(const char* what);
    void syntax_error(const String& message, Optional<Position> = {});
    Token consume();
    Token consume_identifier();
    Token consume_identifier_reference();
    Token consume(TokenType type);
    Token consume_and_validate_numeric_literal();
    void consume_or_insert_semicolon();
    void save_state();
    void load_state();
    void discard_saved_state();
    Position position() const;

    NodePtr<BindingPattern> synthesize_binding_pattern(Expression const& expression);

    Token next_token(size_t steps = 1) const;

    void check_identifier_name_for_assignment_validity(StringView, bool force_strict = false);

    bool try_parse_arrow_function_expression_failed_at_position(const Position&) const;
    void set_try_parse_arrow_function_expression_failed_at_position(const Position&, bool);

    bool match_invalid_escaped_keyword() const;

    bool parse_directive(ScopeNode& body);
    void parse_statement_list(ScopeNode& output_node, AllowLabelledFunction allow_labelled_functions = AllowLabelledFunction::No);

    FlyString consume_string_value();
    ModuleRequest parse_module_request();

    struct RulePosition {
        AK_MAKE_NONCOPYABLE(RulePosition);
        AK_MAKE_NONMOVABLE(RulePosition);

    public:
        RulePosition(Parser& parser, Position position)
            : m_parser(parser)
            , m_position(position)
        {
            m_parser.m_rule_starts.append(position);
        }

        ~RulePosition()
        {
            auto last = m_parser.m_rule_starts.take_last();
            VERIFY(last.line == m_position.line);
            VERIFY(last.column == m_position.column);
        }

        const Position& position() const { return m_position; }

    private:
        Parser& m_parser;
        Position m_position;
    };

    [[nodiscard]] RulePosition push_start() { return { *this, position() }; }

    class PositionKeyTraits {
    public:
        static int hash(const Position& position)
        {
            return int_hash(position.line) ^ int_hash(position.column);
        }

        static bool equals(const Position& a, const Position& b)
        {
            return a.column == b.column && a.line == b.line;
        }
    };

    Vector<Position> m_rule_starts;
    ParserState m_state;
    FlyString m_filename;
    Vector<ParserState> m_saved_state;
    HashMap<Position, TokenMemoization, PositionKeyTraits> m_token_memoizations;
    Program::Type m_program_type;
    size_t m_expression_nesting_level { 0 };
    NonnullNodePtrVector<ParseThunk> m_all_thunks;
    StackInfo m_stack_info;
};
}
