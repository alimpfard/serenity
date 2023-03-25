/*
 * Copyright (c) 2023, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TextMateHighlighter.h"
#include <LibXML/Parser/Parser.h>

namespace Syntax::TextMateImpl {

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

ErrorOr<Rules> Rules::parse(XML::Node const& node)
{
    // plist.dict
    node.
}

}
