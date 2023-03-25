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
};

struct Rules {
    static ErrorOr<Rules> parse_from_xml(StringView);
    static ErrorOr<Rules> parse(XML::Node const&);

    String scope_name;
    Vector<String, 1> file_types;
    Optional<String> first_line_match;
    HashMap<String, Vector<Rule>> repository;
    Vector<Rule> rules;
};
}

class TextMateHighlighter final : public Highlighter {
public:
    explicit TextMateHighlighter(TextMateImpl::Rules);

private:
    virtual Language language() const override { return Language::Custom; }
    virtual ErrorOr<Optional<String>> language_descriptor_name() const override;
    virtual Optional<StringView> comment_prefix() const override;
    virtual Optional<StringView> comment_suffix() const override;
    virtual void rehighlight(Palette const&) override;
    virtual bool token_types_equal(u64, u64) const override;
    virtual Vector<MatchingTokenPair> matching_token_pairs_impl() const override;
};

}
