/*
 * Copyright (c) 2023, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Highlighter.h"
#include <LibXML/Forward.h>

namespace Syntax {

namespace TextMateImpl {

struct MatchRule;
struct BeginEndRule;
struct IncludeRule;
using Rule = Variant<NonnullOwnPtr<MatchRule>, NonnullOwnPtr<BeginEndRule>, NonnullOwnPtr<IncludeRule>>;
using RulePtr = Variant<MatchRule*, BeginEndRule*, IncludeRule*>;

struct MatchRule {
    String name;
    Regex<ECMA262> pattern;
    Vector<String> captures; // idx -> selector
};

struct BeginEndRule {
    String name;
    Regex<ECMA262> begin_pattern;
    Regex<ECMA262> end_pattern;
    Vector<String> begin_captures; // idx -> selector
    Vector<String> end_captures;   // idx -> selector
    Optional<String> content_name;
    Vector<Rule> patterns;
    Vector<RulePtr> pattern_pointers;
};

struct IncludeRule {
    struct SelfReference { };
    struct RepositoryReference {
        String name;
    };
    struct ExternalReference {
        String source;
    };

    Variant<SelfReference, RepositoryReference, ExternalReference> reference;
    String reference_text;
};

struct Rules {
    static ErrorOr<Rules> parse_from_xml(StringView);
    static ErrorOr<Rules> parse(XML::Node const&);

    String name;
    String scope_name;
    Vector<String, 1> file_types;
    Optional<String> first_line_match;
    HashMap<String, Rule> repository;
    Vector<Rule> rules;
    Vector<RulePtr> rule_pointers;
};
}

class TextMateHighlighter final : public Highlighter {
public:
    explicit TextMateHighlighter(TextMateImpl::Rules);

private:
    virtual Language language() const override { return Language::Custom; }
    virtual ErrorOr<Optional<String>> language_descriptor_name() const override;
    virtual Optional<StringView> comment_prefix() const override { return {}; }
    virtual Optional<StringView> comment_suffix() const override { return {}; }
    virtual void rehighlight(Palette const&) override;
    virtual bool token_types_equal(u64, u64) const override;
    virtual Vector<MatchingTokenPair> matching_token_pairs_impl() const override;

    bool execute_rules(Palette const&, RegexStringView&, Vector<GUI::TextDocumentSpan>&, size_t& start_offset, Vector<TextMateImpl::RulePtr>&, size_t line_number) const;
    bool execute_rule(Palette const&, RegexStringView&, Vector<GUI::TextDocumentSpan>&, size_t& start_offset, TextMateImpl::RulePtr, size_t line_number) const;
    void extract_spans(Palette const&, Vector<GUI::TextDocumentSpan>&, Vector<String> const&, Vector<regex::Match> const&, regex::Match const& match, size_t line_number) const;

    mutable TextMateImpl::Rules m_rules;
    mutable Vector<TextMateImpl::BeginEndRule*> m_active_rule_stack;
    mutable HashMap<TextMateImpl::BeginEndRule*, Vector<GUI::TextPosition>> m_active_rule_start_positions;
};

}
