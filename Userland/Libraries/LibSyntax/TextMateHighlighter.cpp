/*
 * Copyright (c) 2023, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TextMateHighlighter.h"
#include <AK/Demangle.h>
#include <AK/ScopeLogger.h>
#include <AK/Tuple.h>
#include <LibXML/Parser/Parser.h>
#include <typeinfo>

static constexpr auto default_re_options {
    (regex::ECMAScriptFlags)regex::AllFlags::SingleMatch
    | (regex::ECMAScriptFlags)regex::AllFlags::Global
    | (regex::ECMAScriptFlags)regex::AllFlags::Multiline
    | (regex::ECMAScriptFlags)regex::AllFlags::Internal_Stateful
    | (regex::ECMAScriptFlags)regex::AllFlags::SkipTrimEmptyMatches
    | regex::ECMAScriptFlags::BrowserExtended
};

namespace Syntax::TextMateImpl {

// This should really be done with a proper query language,
// but let's just make an ad-hoc language for now.
struct QueryResult {
    using Context = Variant<XML::Node const*, XML::Node::Text const*, Tuple<DeprecatedString, DeprecatedString>>;

    Vector<StringView> texts() const
    {
        Vector<StringView> texts;
        for (auto& context : m_nodes) {
            if (auto text = context.get_pointer<XML::Node::Text const*>(); text)
                texts.append((*text)->builder.string_view());
        }
        return texts;
    }

    ErrorOr<Vector<String>> texts_as_strings() const
    {
        Vector<String> texts;
        for (auto& context : m_nodes) {
            if (auto text = context.get_pointer<XML::Node::Text const*>(); text)
                texts.append(TRY((*text)->builder.to_string()));
        }
        return texts;
    }

    StringView text() const
    {
        for (auto& context : m_nodes) {
            if (auto text = context.get_pointer<XML::Node::Text const*>(); text)
                return (*text)->builder.string_view();
        }

        VERIFY_NOT_REACHED();
    }

    Optional<StringView> maybe_text() const
    {
        for (auto& context : m_nodes) {
            if (auto text = context.get_pointer<XML::Node::Text const*>(); text)
                return (*text)->builder.string_view();
        }

        return {};
    }

    Vector<DeprecatedString> attribute_names() const
    {
        Vector<DeprecatedString> names;
        for (auto& context : m_nodes) {
            if (auto attribute = context.get_pointer<Tuple<DeprecatedString, DeprecatedString>>(); attribute)
                names.append(attribute->get<0>());
        }
        return names;
    }

    Vector<DeprecatedString> attribute_values() const
    {
        Vector<DeprecatedString> values;
        for (auto& context : m_nodes) {
            if (auto attribute = context.get_pointer<Tuple<DeprecatedString, DeprecatedString>>(); attribute)
                values.append(attribute->get<1>());
        }
        return values;
    }

    Vector<XML::Node const&> nodes() const
    {
        Vector<XML::Node const&> nodes;
        for (auto& context : m_nodes) {
            if (auto node = context.get_pointer<XML::Node const*>(); node)
                nodes.append(**node);
        }
        return nodes;
    }

    Vector<Context, 1> m_nodes;
};

static auto query_node(XML::Node const& root, StringView query)
{
    // query :: Step? ('/' Step)*
    // Step :: ('.' | ForwardStep | BackwardStep) Test
    // ForwardStep :: Name
    // BackwardStep :: '..'
    // Test :: '*' | 'attribute()' | 'text()'
    // Name :: [a-zA-Z_][a-zA-Z0-9_]*
    // Number :: [0-9]+

    using Context = QueryResult::Context;
    Vector<Context, 1> context_node;
    context_node.append(&root);

    GenericLexer lexer(query);
    while (!lexer.is_eof()) {
        lexer.ignore_while(is_ascii_space);

        if (lexer.consume_specific("..")) {
            Vector<Context, 1> new_context_node;
            for (auto& context : context_node) {
                if (auto node = context.get_pointer<XML::Node const*>(); node && (*node)->parent)
                    new_context_node.append((*node)->parent);
            }
            context_node = move(new_context_node);
            goto test;
        }

        if (lexer.consume_specific(".")) {
            goto test;
        }

        if (lexer.consume_specific("*")) {
            // Bring all children to the top level
            Vector<Context, 1> new_context_node;
            for (auto context : context_node) {
                if (auto node = context.get_pointer<XML::Node const*>()) {
                    if (auto element = (*node)->content.get_pointer<XML::Node::Element>()) {
                        for (auto& child : element->children)
                            new_context_node.append(&*child);
                    }
                }
            }
            context_node = move(new_context_node);
            goto test;
        }

        if (lexer.next_is(is_ascii_alphanumeric)) {
            auto name = lexer.consume_while(is_ascii_alphanumeric);
            Vector<Context, 1> new_context_node;
            for (auto context : context_node) {
                if (auto node = context.get_pointer<XML::Node const*>()) {
                    if (auto element = (*node)->content.get_pointer<XML::Node::Element>()) {
                        for (auto& child : element->children) {
                            if (auto e = child->content.get_pointer<XML::Node::Element>(); e && e->name == name)
                                new_context_node.append(&*child);
                        }
                    }
                }
            }
            context_node = move(new_context_node);
            goto test;
        }

    test:;
        lexer.ignore_while(is_ascii_space);
        if (lexer.is_eof())
            break;

        if (lexer.consume_specific("/"))
            continue;

        if (lexer.consume_specific("attribute()")) {
            Vector<Context, 1> new_context_node;
            for (auto context : context_node) {
                if (auto node = context.get_pointer<XML::Node const*>()) {
                    if (auto element = (*node)->content.get_pointer<XML::Node::Element>()) {
                        for (auto& attribute : element->attributes)
                            new_context_node.append(Tuple { attribute.key, attribute.value });
                    }
                }
            }
            context_node = move(new_context_node);
        } else if (lexer.consume_specific("text()")) {
            Vector<Context, 1> new_context_node;
            for (auto context : context_node) {
                if (auto node = context.get_pointer<XML::Node const*>()) {
                    if (auto text = (*node)->content.get_pointer<XML::Node::Text>()) {
                        new_context_node.append(text);
                    }
                }
            }
            context_node = move(new_context_node);
        } else if (lexer.consume_specific("element()")) {
            Vector<Context, 1> new_context_node;
            for (auto context : context_node) {
                if (auto node = context.get_pointer<XML::Node const*>()) {
                    if ((*node)->content.has<XML::Node::Element>()) {
                        new_context_node.append(*node);
                    }
                }
            }
            context_node = move(new_context_node);
        } else {
            VERIFY_NOT_REACHED();
        }
    }

    return QueryResult { move(context_node) };
}

ErrorOr<Rules> Rules::parse_from_xml(StringView contents)
{
    auto document_or_error = XML::Parser { contents }.parse();
    if (document_or_error.is_error()) {
        dbgln("Failed to parse TextMate grammar XML: {}", document_or_error.error());
        return Error::from_string_literal("Failed to parse TextMate grammar XML");
    }

    auto document = document_or_error.release_value();
    return parse(document.root());
}

template<typename T>
auto flatten(Optional<T>&& optional)
{
    if constexpr (IsSpecializationOf<T, Optional>) {
        using R = RemoveCVReference<decltype(flatten(optional.release_value()))>;
        if (optional.has_value())
            return flatten(optional.release_value());
        else
            return R { OptionalNone() };
    } else {
        return move(optional);
    }
}

static ErrorOr<Rule> parse_rule(XML::Node const& root);
static ErrorOr<Vector<Rule>> parse_patterns(XML::Node const& array);

static ErrorOr<Rule> parse_include(HashMap<StringView, XML::Node const*>& properties)
{
    auto reference = flatten(properties.get("include"sv).map([](auto node) { return query_node(*node, "* text()"sv).maybe_text(); }));
    if (!reference.has_value()) {
        dbgln("Expected string in include");
        return Error::from_string_literal("Expected string in include");
    }

    if (reference == "$self"sv)
        return try_make<IncludeRule>(IncludeRule::SelfReference(), String::from_utf8(*reference).release_value());

    if (reference->starts_with('#'))
        return try_make<IncludeRule>(IncludeRule::RepositoryReference { TRY(String::from_utf8(reference->substring_view(1))) }, String::from_utf8(*reference).release_value());

    return try_make<IncludeRule>(IncludeRule::ExternalReference { TRY(String::from_utf8(*reference)) }, String::from_utf8(*reference).release_value());
}

static ErrorOr<Rule> parse_match(HashMap<StringView, XML::Node const*>& properties)
{
    auto pattern = flatten(properties.get("match"sv).map([](auto node) { return query_node(*node, "* text()"sv).maybe_text(); }));
    if (!pattern.has_value()) {
        dbgln("Expected string in match");
        return Error::from_string_literal("Expected string in match");
    }

    auto name = flatten(properties.get("name"sv).map([&](auto node) { return query_node(*node, "* text()"sv).maybe_text(); }));
    if (!name.has_value()) {
        dbgln("Expected string in name");
        return Error::from_string_literal("Expected string in name");
    }

    Vector<String> captures;
    auto parse_captures = [&](auto name, auto& captures) -> ErrorOr<void> {
        auto capture_root_keys = flatten(properties.get(name).map([](auto node) { return query_node(*node, "key/* text()"sv).texts(); }));
        auto capture_root_values = flatten(properties.get(name).map([](auto node) { return query_node(*node, "dict/string/* text()"sv).texts(); }));
        if (!capture_root_keys.has_value() || !capture_root_values.has_value())
            return {};

        if (capture_root_keys->size() != capture_root_values->size()) {
            dbgln("Uneven number of children in captures {} keys vs {} values", capture_root_keys->size(), capture_root_values->size());
            return Error::from_string_literal("Uneven number of children in captures");
        }

        for (size_t i = 0; i < capture_root_keys->size(); ++i) {
            auto key = capture_root_keys->at(i).to_uint();
            if (!key.has_value())
                return Error::from_string_literal("Expected number in captures");

            auto value = TRY(String::from_utf8(capture_root_values->at(i)));

            TRY(captures.try_resize(max(captures.size(), *key + 1)));

            captures.insert(*key, move(value));
        }

        return {};
    };

    TRY(parse_captures("captures"sv, captures));

    Regex<ECMA262> re(*pattern, default_re_options);
    if (re.parser_result.error != regex::Error::NoError) {
        dbgln("Failed to parse regex: {}", re.error_string());
        dbgln("Pattern: '{}'", *pattern);
    }

    return try_make<MatchRule>(MatchRule { TRY(String::from_utf8(*name)), move(re), move(captures) });
}

static ErrorOr<Rule> parse_begin_end(HashMap<StringView, XML::Node const*>& properties)
{
    auto begin = flatten(properties.get("begin"sv).map([](auto node) { return query_node(*node, "* text()"sv).maybe_text(); }));
    if (!begin.has_value()) {
        dbgln("Expected string in begin");
        return Error::from_string_literal("Expected string in begin");
    }

    auto end = flatten(properties.get("end"sv).map([](auto node) { return query_node(*node, "* text()"sv).maybe_text(); }));
    if (!end.has_value()) {
        dbgln("Expected string in end");
        return Error::from_string_literal("Expected string in end");
    }

    auto name = flatten(properties.get("name"sv).map([](auto node) { return query_node(*node, "* text()"sv).maybe_text(); })).value_or({});
    auto content_name = flatten(TRY(flatten(properties.get("name"sv).map([](auto node) { return query_node(*node, "* text()"sv).maybe_text(); })).map([](auto& string) -> ErrorOr<Optional<String>> { return TRY(String::from_utf8(string)); })));

    Vector<String> begin_captures;
    Vector<String> end_captures;

    auto parse_captures = [&](auto name, auto& captures) -> ErrorOr<void> {
        auto capture_root_keys = flatten(properties.get(name).map([](auto node) { return query_node(*node, "key/* text()"sv).texts(); }));
        auto capture_root_values = flatten(properties.get(name).map([](auto node) { return query_node(*node, "dict/string/* text()"sv).texts(); }));
        if (!capture_root_keys.has_value() || !capture_root_values.has_value())
            return {};

        if (capture_root_keys->size() != capture_root_values->size()) {
            dbgln("Uneven number of children in captures {} keys vs {} values", capture_root_keys->size(), capture_root_values->size());
            return Error::from_string_literal("Uneven number of children in captures");
        }

        for (size_t i = 0; i < capture_root_keys->size(); ++i) {
            auto key = capture_root_keys->at(i).to_uint();
            if (!key.has_value())
                return Error::from_string_literal("Expected number in captures");

            auto value = TRY(String::from_utf8(capture_root_values->at(i)));

            TRY(captures.try_resize(max(captures.size(), *key + 1)));

            captures.insert(*key, move(value));
        }

        return {};
    };

    TRY(parse_captures("beginCaptures"sv, begin_captures));
    TRY(parse_captures("captures"sv, begin_captures)); // Alias :shrug:
    TRY(parse_captures("endCaptures"sv, end_captures));

    Regex<ECMA262> re_begin(*begin, default_re_options);
    if (re_begin.parser_result.error != regex::Error::NoError) {
        dbgln("Failed to parse regex: {}", re_begin.error_string());
        dbgln("Pattern: '{}'", *begin);
    }

    Regex<ECMA262> re_end(*end, default_re_options);
    if (re_end.parser_result.error != regex::Error::NoError) {
        dbgln("Failed to parse regex: {}", re_end.error_string());
        dbgln("Pattern: '{}'", *end);
    }

    if (auto patterns_node = properties.get("patterns"sv); patterns_node.has_value()) {
        auto patterns = TRY(parse_patterns(*patterns_node.value()));
        auto pattern_pointers = Vector<RulePtr>();
        TRY(pattern_pointers.try_ensure_capacity(patterns.size()));
        for (auto& pattern : patterns)
            pattern_pointers.append(pattern.visit([](auto& x) -> RulePtr { return &*x; }));

        return try_make<BeginEndRule>(BeginEndRule {
            TRY(String::from_utf8(name)),
            move(re_begin),
            move(re_end),
            move(begin_captures),
            move(end_captures),
            move(content_name),
            move(patterns),
            move(pattern_pointers),
        });
    }

    return try_make<BeginEndRule>(BeginEndRule {
        TRY(String::from_utf8(name)),
        move(re_begin),
        move(re_end),
        move(begin_captures),
        move(end_captures),
        move(content_name),
        {},
        {},
    });
}

ErrorOr<Rule> parse_rule(XML::Node const& root)
{
    auto root_dict = query_node(root, "* element()"sv).nodes();
    if (root_dict.size() % 2 != 0) {
        dbgln("Expected even number of children in rule");
        return Error::from_string_literal("Expected even number of children in rule");
    }

    HashMap<StringView, XML::Node const*> properties;
    enum {
        Unknown,
        Include,
        Match,
        BeginEnd,
    } kind { Unknown };

    for (size_t i = 0; i < root_dict.size(); i += 2) {
        auto key = query_node(root_dict[i], "* text()"sv).text();
        auto& value = root_dict[i + 1];

        properties.set(key, &value);
        if (key == "include"sv) {
            if (kind != Unknown) {
                dbgln("Expected only one of 'include', 'match' or 'begin/end'");
                return Error::from_string_literal("Expected only one of 'include', 'match' or 'begin/end'");
            }
            kind = Include;
        } else if (key == "match"sv) {
            if (kind != Unknown) {
                dbgln("Expected only one of 'include', 'match' or 'begin/end'");
                return Error::from_string_literal("Expected only one of 'include', 'match' or 'begin/end'");
            }
            kind = Match;
        } else if (key == "begin"sv || key == "end"sv) {
            if (kind != Unknown && kind != BeginEnd) {
                dbgln("Expected only one of 'include', 'match' or 'begin/end'");
                return Error::from_string_literal("Expected only one of 'include', 'match' or 'begin/end'");
            }
            kind = BeginEnd;
        }
    }

    switch (kind) {
    case Unknown:
        return try_make<MatchRule>(MatchRule { String(), Regex<ECMA262>("$.^"), {} });
    case Include:
        return parse_include(properties);
    case Match:
        return parse_match(properties);
    case BeginEnd:
        return parse_begin_end(properties);
    }

    VERIFY_NOT_REACHED();
}

ErrorOr<Vector<Rule>> parse_patterns(XML::Node const& array)
{
    Vector<Rule> rules;
    for (auto& pattern_node : query_node(array, "dict"sv).nodes())
        rules.append(TRY(parse_rule(pattern_node)));

    return rules;
}

static ErrorOr<HashMap<String, Rule>> parse_repository(XML::Node const& dict)
{
    HashMap<String, Rule> repository;
    auto nodes = query_node(dict, "* element()"sv).nodes();
    for (size_t i = 0; i < nodes.size(); i += 2) {
        auto key = query_node(nodes[i], "* text()"sv).text();
        auto& value = nodes[i + 1];
        repository.set(TRY(String::from_utf8(key)), TRY(parse_rule(value)));
    }

    return repository;
}

ErrorOr<Rules> Rules::parse(XML::Node const& node)
{
    auto root_dict = query_node(node, "dict/* element()"sv).nodes();
    if (root_dict.size() % 2 != 0) {
        dbgln("Expected an even number of elements in plist/dict");
        return Error::from_string_literal("Expected an even number of elements in plist/dict");
    }

    Rules rules {};

    for (size_t i = 0; i < root_dict.size(); i += 2) {
        auto key = query_node(root_dict[i], "* text()"sv).text();
        auto& value = root_dict[i + 1];

        if (key == "name"sv) {
            rules.name = TRY(String::from_utf8(query_node(value, "* text()"sv).text()));
        } else if (key == "scopeName"sv) {
            rules.scope_name = TRY(String::from_utf8(query_node(value, "* text()"sv).text()));
        } else if (key == "fileTypes"sv) {
            rules.file_types = TRY(query_node(value, "string text()"sv).texts_as_strings());
        } else if (key == "patterns"sv) {
            rules.rules = TRY(parse_patterns(value));
        } else if (key == "repository"sv) {
            rules.repository = TRY(parse_repository(value));
        } else {
            dbgln("Unknown key in plist/dict: {}", key);
        }
    }

    TRY(rules.rule_pointers.try_ensure_capacity(rules.rules.size()));
    for (auto& rule : rules.rules)
        rules.rule_pointers.append(rule.visit([](auto& x) -> RulePtr { return &*x; }));

    return rules;
}

}

namespace Syntax {

TextMateHighlighter::TextMateHighlighter(TextMateImpl::Rules rules)
    : m_rules(move(rules))
{
}

ErrorOr<Optional<String>> TextMateHighlighter::language_descriptor_name() const
{
    return m_rules.name;
}

bool TextMateHighlighter::token_types_equal(u64 a, u64 b) const
{
    return a == b;
}

Vector<Highlighter::MatchingTokenPair> TextMateHighlighter::matching_token_pairs_impl() const
{
    return {};
}

static Gfx::TextAttributes translate_scope_name(String const& name, Palette const& palette)
{
    Gfx::TextAttributes attributes;
    attributes.color = palette.base_text();

    auto full_scope = name.bytes_as_string_view();
    auto scope = full_scope.find_first_split_view('.');
    if (scope == "comment"sv) {
        attributes.color = palette.syntax_comment();
    } else if (scope == "constant"sv) {
        if (full_scope.starts_with("constant.numeric"sv))
            attributes.color = palette.syntax_number();
        else if (full_scope.starts_with("constant.character"sv))
            attributes.color = palette.syntax_string();
        else if (full_scope.starts_with("constant.language"sv))
            attributes.color = palette.syntax_keyword();
        else
            attributes.color = palette.syntax_identifier();
    } else if (scope == "entity"sv) {
        attributes.color = palette.syntax_identifier();
        attributes.bold = true;
    } else if (scope == "invalid"sv) {
        attributes.background_color = palette.bright_red();
    } else if (scope == "keyword"sv) {
        if (full_scope.starts_with("keyword.control"sv))
            attributes.color = palette.syntax_control_keyword();
        else if (full_scope.starts_with("keyword.operator"sv))
            attributes.color = palette.syntax_operator();
        else
            attributes.color = palette.syntax_keyword();
    } else if (scope == "markup"sv) {
        if (full_scope.starts_with("markup.underline.link"sv)) {
            attributes.underline_style = Gfx::TextAttributes::UnderlineStyle::Wavy;
            attributes.underline_color = palette.base_text();
        } else if (full_scope.starts_with("markup.underline"sv)) {
            attributes.underline_style = Gfx::TextAttributes::UnderlineStyle::Solid;
            attributes.underline_color = palette.base_text();
        } else if (full_scope.starts_with("markup.bold"sv)) {
            attributes.bold = true;
        }
    } else if (scope == "meta"sv) {
        // This has no visual representation.
    } else if (scope == "storage"sv) {
        if (full_scope.starts_with("storage.type"sv))
            attributes.color = palette.syntax_type();
        else
            attributes.color = palette.syntax_keyword();
    } else if (scope == "string"sv) {
        attributes.color = palette.syntax_string();
    } else if (scope == "support"sv) {
        if (full_scope.starts_with("support.function"sv))
            attributes.color = palette.syntax_function();
        else if (full_scope.starts_with("support.type"sv))
            attributes.color = palette.syntax_type();
        else if (full_scope.starts_with("support.variable"sv))
            attributes.color = palette.syntax_variable();
        else
            attributes.color = palette.syntax_identifier();
    } else if (scope == "variable"sv) {
        attributes.color = palette.syntax_variable();
    } else if (scope == "punctuation"sv) {
        attributes.color = palette.syntax_punctuation();
    }

    return attributes;
}

static DeprecatedString print_rule(TextMateImpl::RulePtr rule)
{
    return rule.visit(
        [&](TextMateImpl::MatchRule* rule) { return DeprecatedString::formatted("match({})", rule->pattern.pattern_value); },
        [&](TextMateImpl::BeginEndRule* rule) { return DeprecatedString::formatted("begin-end(begin={}, end={})", rule->begin_pattern.pattern_value, rule->end_pattern.pattern_value); },
        [&](TextMateImpl::IncludeRule* rule) { return DeprecatedString::formatted("include({})", rule->reference_text); });
}

void TextMateHighlighter::rehighlight(Palette const& palette)
{
    auto text = m_client->get_text();
    auto lines = RegexStringView(text).lines();

    Vector<GUI::TextDocumentSpan> spans;

    size_t line_number = 0;
    size_t start_offset = 0;
    for (auto line : lines) {
        start_offset = 0;
        while (execute_rules(palette, line, spans, start_offset, m_rules.rule_pointers, line_number))
            ;

        dbgln("Line loop finished, in {} active rules:", m_active_rule_stack.size());
        for (auto& rule : m_active_rule_stack)
            dbgln("  begin-end(begin={}, end={})", rule->begin_pattern.pattern_value, rule->end_pattern.pattern_value);

        line_number++;
    }

    m_active_rule_stack.clear();

    // Break overlapping spans into multiple spans.
    Vector<GUI::TextDocumentSpan> new_spans;
    for (auto& span : spans) {
        if (new_spans.is_empty()) {
            new_spans.append(span);
            continue;
        }
        auto& last_span = new_spans.last();
        if (last_span.range.end() < span.range.start()) {
            new_spans.append(span);
            continue;
        }
        if (last_span.range.end() == span.range.start()) {
            last_span.attributes = span.attributes;
            last_span.range.set_end(span.range.end());
            continue;
        }
        if (last_span.range.end() > span.range.start()) {
            auto new_span = span;
            new_span.range.set_start(last_span.range.end());
            new_spans.append(new_span);
            last_span.range.set_end(span.range.start());
            continue;
        }
        VERIFY_NOT_REACHED();
    }

    // dbgln("Spans:");
    // for (auto& span : spans)
    //     dbgln("  {}-{} -- {}-{}: {}",
    //         span.range.start().line(), span.range.start().column(),
    //         span.range.end().line(), span.range.end().column(),
    //         span.attributes.color.to_deprecated_string());
    m_client->do_set_spans(move(new_spans));
}

bool TextMateHighlighter::execute_rule(Palette const& palette, RegexStringView& text, Vector<GUI::TextDocumentSpan>& spans, size_t& start_offset, TextMateImpl::RulePtr rule, size_t line_number) const
{
    dbgln("Running rule {}, offset={}", print_rule(rule), start_offset);
    // auto& the_rule = rule;
    auto result = rule.visit(
        [&](TextMateImpl::MatchRule* rule) -> bool {
            rule->pattern.start_offset = start_offset;
            auto result = rule->pattern.match(text);
            if (!result.success)
                return false;

            dbgln("rule {} matched", print_rule(rule));
            start_offset = rule->pattern.start_offset;

            extract_spans(palette, spans, rule->captures, result.capture_group_matches[0], result.matches[0], line_number);

            return true;
        },
        [&](TextMateImpl::BeginEndRule* rule) -> bool {
            rule->begin_pattern.start_offset = start_offset;
            auto result = rule->begin_pattern.match(text);
            if (!result.success)
                return false;

            dbgln("rule {} begin matched, offset={}", print_rule(rule), start_offset);

            start_offset = rule->begin_pattern.start_offset;
            extract_spans(palette, spans, rule->begin_captures, result.capture_group_matches[0], result.matches[0], line_number);

            m_active_rule_stack.append(rule);
            if (!rule->name.is_empty())
                m_active_rule_start_positions.ensure(rule).append({ line_number, result.matches[0].left_column });

            return true;
        },
        [&](TextMateImpl::IncludeRule* rule) -> bool {
            return rule->reference.visit(
                [&](TextMateImpl::IncludeRule::SelfReference) -> bool {
                    return execute_rules(palette, text, spans, start_offset, m_rules.rule_pointers, line_number);
                },
                [&](TextMateImpl::IncludeRule::RepositoryReference const& reference) -> bool {
                    auto it = m_rules.repository.find(reference.name);
                    if (it == m_rules.repository.end()) {
                        dbgln("Failed to find repository reference {}", reference.name);
                        return false;
                    }

                    return execute_rule(palette, text, spans, start_offset, it->value.visit([](auto& x) -> TextMateImpl::RulePtr { return &*x; }), line_number);
                },
                [&](TextMateImpl::IncludeRule::ExternalReference const& reference) -> bool {
                    dbgln("External references are not supported yet ({})", reference.source);
                    return false;
                });
        });

    // dbgln("execute_rule({}) -> offset={}, \x1b[33m{}\x1b[0m", print_rule(rule), start_offset, result);
    return result;
}

bool TextMateHighlighter::execute_rules(Palette const& palette, RegexStringView& text, Vector<GUI::TextDocumentSpan>& spans, size_t& start_offset, Vector<TextMateImpl::RulePtr>& rules, size_t line_number) const
{
    auto execute_active_rule = [&](auto& line) {
        if (!m_active_rule_stack.is_empty()) {
            auto current_rules = move(m_active_rule_stack);
            ScopeGuard restore_active_rules = [&] {
                m_active_rule_stack = move(current_rules);
            };

            auto active_rule = current_rules.last();
            while (execute_rules(palette, line, spans, start_offset, active_rule->pattern_pointers, line_number))
                ;

            active_rule->end_pattern.start_offset = start_offset;
            auto result = active_rule->end_pattern.match(text);
            if (result.success) {
                start_offset = active_rule->end_pattern.start_offset;

                dbgln("rule {} end matched, offset={}", print_rule(active_rule), start_offset);
                current_rules.take_last();

                extract_spans(palette, spans, active_rule->end_captures, result.capture_group_matches[0], result.matches[0], line_number);

                if (!active_rule->name.is_empty()) {
                    spans.append(GUI::TextDocumentSpan {
                        .range = {
                            m_active_rule_start_positions.get(active_rule).value().take_last(),
                            { line_number, result.matches[0].column + result.matches[0].view.length_in_code_units() },
                        },
                        .attributes = translate_scope_name(active_rule->name, palette),
                        .data = active_rule->name.hash(),
                    });
                }
            }
        }
    };

    execute_active_rule(text);

    for (auto& rule : rules) {
        if (execute_rule(palette, text, spans, start_offset, rule, line_number)) {
            execute_active_rule(text);
            return true;
        }
    }

    return false;
}

void TextMateHighlighter::extract_spans(Palette const& palette, Vector<GUI::TextDocumentSpan>& spans, Vector<String> const& captures, Vector<regex::Match> const& groups, regex::Match const& match, size_t line_number) const
{
    for (size_t i = 0; i < captures.size(); ++i) {
        auto& name = captures[i];
        if (name.is_empty())
            continue;

        regex::Match const* group = nullptr;
        if (i == 0) {
            group = &match;
        } else {
            auto effective_index = i - 1;
            if (groups.size() <= effective_index)
                continue;
            group = &groups[effective_index];
        }

        if (group->view.length_in_code_units() == 0)
            continue;

        spans.append(GUI::TextDocumentSpan {
            .range = {
                { line_number, group->column },
                { line_number, group->column + group->view.length_in_code_units() },
            },
            .attributes = translate_scope_name(name, palette),
            .data = name.hash(),
        });
    }
}

}
