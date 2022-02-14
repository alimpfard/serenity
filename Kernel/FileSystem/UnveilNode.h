/*
 * Copyright (c) 2020, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <AK/Trie.h>

namespace Kernel {

enum UnveilAccess {
    Read = 1,
    Write = 2,
    Execute = 4,
    CreateOrRemove = 8,
    Browse = 16,

    None = 0,
};

struct UnveilNode;

struct UnveilMetadata {
    Variant<OwnPtr<KString>, StringView> full_path;
    UnveilAccess permissions { None };
    bool explicitly_unveiled { false };

    UnveilMetadata(UnveilMetadata const&) = delete;
    UnveilMetadata(UnveilMetadata&&) = default;

    // Note: Intentionally not explicit.
    UnveilMetadata(Variant<OwnPtr<KString>, StringView>&& full_path, UnveilAccess permissions = None, bool explicitly_unveiled = false)
        : full_path(move(full_path))
        , permissions(permissions)
        , explicitly_unveiled(explicitly_unveiled)
    {
    }

    ErrorOr<UnveilMetadata> copy() const
    {
        return UnveilMetadata {
            TRY(full_path.visit(
                [](StringView string) -> ErrorOr<Variant<OwnPtr<KString>, StringView>> {
                    return Variant<OwnPtr<KString>, StringView>(TRY(KString::try_create(string)));
                },
                [](OwnPtr<KString> const& string) -> ErrorOr<Variant<OwnPtr<KString>, StringView>> {
                    if (!string)
                        return Variant<OwnPtr<KString>, StringView>(OwnPtr<KString>());

                    return Variant<OwnPtr<KString>, StringView>(TRY(string->try_clone()));
                })),
            permissions,
            explicitly_unveiled,
        };
    }
};

struct UnveilNode final : public Trie<String, UnveilMetadata, Traits<String>, UnveilNode> {
    using Trie<String, UnveilMetadata, Traits<String>, UnveilNode>::Trie;

    bool was_explicitly_unveiled() const { return this->metadata_value().explicitly_unveiled; }
    UnveilAccess permissions() const { return this->metadata_value().permissions; }
    StringView path() const
    {
        return this->metadata_value().full_path.visit(
            [](OwnPtr<KString> const& string) { return string->view(); },
            [](StringView string) { return string; });
    }
};

}
