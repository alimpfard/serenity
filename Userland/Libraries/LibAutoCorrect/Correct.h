/*
 * Copyright (c) 2022, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/DistinctNumeric.h>
#include <AK/String.h>
#include <AK/Trie.h>
#include <AK/Utf32View.h>
#include <AK/Vector.h>

namespace AutoCorrect {
TYPEDEF_DISTINCT_NUMERIC_GENERAL(u32, false, true, false, false, false, false, EncodedCodePoint);
}

template<>
struct AK::Traits<AutoCorrect::EncodedCodePoint> : public AK::GenericTraits<AutoCorrect::EncodedCodePoint> {
    static constexpr unsigned hash(AutoCorrect::EncodedCodePoint e) { return int_hash(e.value()); }
};

namespace AutoCorrect {
struct Result {
    Utf32View suggestion;
    float probability;
};

class WordTree {
public:
    static ErrorOr<WordTree> load_from_file(String const& data_path);

    ErrorOr<void> save_to_file(String path);

    bool insert(Utf32View string);
    bool has(Utf32View string) const;
    Optional<Vector<Result>> filter_for(Utf32View string, size_t max_to_fetch = 16);

    size_t alphabet_count() const { return m_alphabet_count; };
    u32 alphabet_first_code_point() const { return m_alphabet_first_code_point; };

private:
    WordTree() = default;

    struct DictionaryView {
        size_t offset { 0 };
        size_t length { 0 };

        Utf32View view(WordTree const& tree) const
        {
            return {
                tree.m_dictionary_storage.data() + offset,
                length,
            };
        }

        bool is_empty() const { return length == 0; }
    };
    struct NodeMetadata {
        bool enabled;
        float projected_probability;
        DictionaryView data;
    };

    struct Node final : public Trie<EncodedCodePoint, NodeMetadata, Traits<EncodedCodePoint>, Node> {
        using Trie<EncodedCodePoint, NodeMetadata, Traits<EncodedCodePoint>, Node>::Trie;
    };

    void filter_for_impl(Node&, Utf32View string);

    struct FilterMapData {
        EncodedCodePoint value;
        float transition_probability;
    };

    Vector<FilterMapData> const& filter_data_for(EncodedCodePoint code_point) const
    {
        static Vector<FilterMapData> empty {};

        if (m_filter_map_data.size() <= code_point.value()) {
            dbgln("Filter map looking up (offset) code point {}, which is not part of our map data", code_point.value());
            return empty;
        }

        return m_filter_map_data[code_point.value()];
    }

    Vector<Vector<FilterMapData>> m_filter_map_data;
    Node m_root { 0, { true, 1.f, {} } };
    Vector<u32> m_dictionary_storage;
    size_t m_filled { 0 };
    size_t m_alphabet_count { 0 };
    u32 m_alphabet_first_code_point { 0 };
    float m_minimum_accepted_probability { 0.5f };
    float m_partial_word_probability_multiplier { 0.8f };
};

class AutoCorrect {
public:
    static ErrorOr<AutoCorrect> load_from_file(String const& data_path)
    {
        return AutoCorrect {
            TRY(WordTree::load_from_file(data_path))
        };
    }

    AutoCorrect(WordTree tree)
        : m_tree(move(tree))
    {
    }

    Vector<Result> fetch_corrections(Vector<Utf32View> context_and_word)
    {
        return m_tree.filter_for(context_and_word.last()).value_or({});
    }

private:
    WordTree m_tree;
};
}