/*
 * Copyright (c) 2022, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/DistinctNumeric.h>
#include <AK/Queue.h>
#include <AK/QuickSort.h>
#include <AK/Utf8View.h>
#include <LibAutoCorrect/Correct.h>
#include <LibCore/ElapsedTimer.h>
#include <LibCore/MappedFile.h>

using AutoCorrect::EncodedCodePoint;

constexpr static EncodedCodePoint EOW = 0;

static EncodedCodePoint encode(u32 code_point, AutoCorrect::WordTree const& tree)
{
    if (code_point >= tree.alphabet_first_code_point() && code_point < tree.alphabet_first_code_point() + tree.alphabet_count())
        return code_point - tree.alphabet_first_code_point() + 1;

    return EOW;
}

[[maybe_unused]] static u32 decode(EncodedCodePoint encoded_code_point, AutoCorrect::WordTree const& tree)
{
    if (encoded_code_point == EOW)
        return 0;

    return encoded_code_point.value() - 1 + tree.alphabet_first_code_point();
}

template<typename UnderlyingIteratorT>
struct EncodingIterator {
    UnderlyingIteratorT it;
    AutoCorrect::WordTree const& tree;

    EncodingIterator(UnderlyingIteratorT it, AutoCorrect::WordTree const& tree)
        : it(move(it))
        , tree(tree)
    {
    }

    EncodingIterator& operator++()
    {
        ++it;
        return *this;
    }

    decltype(auto) operator*()
    {
        return encode(*it, tree);
    }

    bool operator==(EncodingIterator const& other) const { return &tree == &other.tree && it == other.it; }
};

namespace AutoCorrect {

struct [[gnu::packed]] TreeData {
    u32 magic;
    u32 first_alphabet_code_point;
    u32 alphabet_count;
    float minimum_accepted_probability;
};

struct [[gnu::packed]] RawFilterMapData {
    struct [[gnu::packed]] Data {
        u32 code_point;
        float transition_probability;
    };

    u32 count;
    Data data[];
};

ErrorOr<WordTree> WordTree::load_from_file(String const& data_path)
{
    auto mapped_file = TRY(Core::MappedFile::map(data_path));
    [[maybe_unused]] auto data = mapped_file->bytes();
    WordTree tree;
    // File format:
    //   (tree format)
    //   (filter data)
    //   (strings, utf8, split by newlines)
    // Tree format:
    //   TreeData
    // Filter data format:
    //   RawFilterMapData * TreeData.alphabet_count
    //   Each index corresponds to code point (index + TreeData.first_alphabet_code_point)

    if (data.size() < sizeof(TreeData))
        return Error::from_string_literal("Invalid WordTree data file, not enough data for tree format");

    auto& tree_data = *bit_cast<TreeData const*>(data.data());
    data = data.slice(sizeof(TreeData));

    if (tree_data.magic != 0x69696969)
        return Error::from_string_literal("Invalid WordTree data file, mismatching magic");

    tree.m_minimum_accepted_probability = tree_data.minimum_accepted_probability;
    tree.m_alphabet_first_code_point = tree_data.first_alphabet_code_point;
    tree.m_alphabet_count = tree_data.alphabet_count;

    if (data.size() < tree_data.alphabet_count * sizeof(RawFilterMapData))
        return Error::from_string_literal("Invalid WordTree data file, not enough data for filters");

    for (size_t i = 0; i < tree_data.alphabet_count; ++i) {
        TRY(tree.m_filter_map_data.try_empend());
        auto& filter_vector = tree.m_filter_map_data.last();

        auto& filter_data = *bit_cast<RawFilterMapData const*>(data.data());
        for (size_t filter_index = 0; filter_index < filter_data.count; ++filter_index) {
            auto& filter = filter_data.data[filter_index];
            TRY(filter_vector.try_append(FilterMapData {
                .value = filter.code_point,
                .transition_probability = filter.transition_probability,
            }));
        }

        data = data.slice(sizeof(RawFilterMapData) + sizeof(RawFilterMapData::Data) * filter_data.count);
    }

    Utf8View utf8_view { StringView { data.data(), data.size() } };
    Vector<u32, 32> current_word;
    for (auto code_point : utf8_view) {
        if (code_point == '\n') {
            auto view = Utf32View { current_word.data(), current_word.size() };
            tree.insert(view);
            current_word.clear_with_capacity();
            continue;
        }
        TRY(current_word.try_append(code_point));
    }

    if (!current_word.is_empty())
        tree.insert(Utf32View { current_word.data(), current_word.size() });

    return { move(tree) };
}

bool WordTree::has(Utf32View string) const
{
    auto it = EncodingIterator { string.begin(), *this };
    auto end = EncodingIterator { string.end(), *this };
    auto const& node = m_root.traverse_until_last_accessible_node(it, end);
    return it == end && !node.metadata_value().data.is_empty();
}

bool WordTree::insert(Utf32View string)
{
    auto it = EncodingIterator { string.begin(), *this };
    auto end = EncodingIterator { string.end(), *this };
    auto& node = m_root.traverse_until_last_accessible_node(it, end);
    if (it == end && !node.metadata_value().data.is_empty())
        return false;
    size_t start_index = m_dictionary_storage.size();
    m_dictionary_storage.append(string.code_points(), string.length());

    NodeMetadata metadata {
        .enabled = false,
        .projected_probability = 0.f,
        .data = { start_index, string.length() },
    };
    node.insert(
        it, end, metadata,
        []<typename... Ts>(Ts && ...)->Optional<NodeMetadata> { return NodeMetadata { false, 0.f, {} }; });

    return true;
}

void WordTree::filter_for_impl(Node& node, Utf32View string)
{
    for (auto& child : node.children())
        child.value->mutable_metadata()->enabled = false;

    auto encoded = string.is_empty() ? 0 : encode(string[0], *this);
    auto rest_of_string = string.is_empty() ? string : string.substring_view(1, string.length() - 1);

    auto probability = node.metadata_value().projected_probability;
    if (encoded == EOW) {
        // Allow continuations.
        for (auto& child : node.children()) {
            child.value->mutable_metadata()->projected_probability = probability;
            if (probability >= m_minimum_accepted_probability)
                child.value->mutable_metadata()->enabled = true;
        }
    }

    for (auto& entry : filter_data_for(encoded)) {
        auto it = node.children().find(entry.value);
        if (it != node.children().end()) {
            auto node_probability = probability * entry.transition_probability;
            if (entry.value == EOW) {
                for (size_t i = 0; i < rest_of_string.length(); ++i)
                    node_probability *= m_partial_word_probability_multiplier;
            }
            it->value->mutable_metadata()->projected_probability = node_probability;
            if (node_probability >= m_minimum_accepted_probability) {
                it->value->mutable_metadata()->enabled = true;
                if (entry.value != EOW)
                    filter_for_impl(static_cast<Node&>(*it->value), rest_of_string);
            }
        }
    }
}

[[maybe_unused]] static size_t levenshtein_distance(Utf32View a, Utf32View b)
{
    if (a.length() == 0)
        return b.length();

    if (b.length() == 0)
        return a.length();

    Vector<u32> cache;
    cache.resize(a.length());

    for (size_t i = 0; i < a.length(); ++i)
        cache[i] = i + 1;

    size_t distance = 0;
    size_t result = 0;
    size_t a_index = 0;
    size_t b_index = 0;

    while (b_index < b.length()) {
        auto code_point = b[b_index];
        distance = b_index++;
        result = distance;
        a_index = 0;
        while (a.length() > a_index) {
            auto b_distance = code_point == a[a_index] ? distance : distance + 1;
            distance = cache[a_index];
            result = distance > result
                ? (b_distance > result
                        ? result + 1
                        : b_distance)
                : (b_distance > distance
                        ? distance + 1
                        : b_distance);

            cache[a_index] = result;
            a_index++;
        }
    }

    return result;
}

Optional<Vector<Result>> WordTree::filter_for(Utf32View string, size_t max_to_fetch)
{
    m_root.mutable_metadata()->enabled = true;
    m_root.mutable_metadata()->projected_probability = 1.f;

    StringBuilder builder;
    builder.append(string);
    if (has(string)) {
        return {};
    }

    dbgln("FUCK {}", builder.string_view());
    Core::ElapsedTimer timer;
    timer.start();
    filter_for_impl(m_root, string);
    dbgln("FUCK DONE {}", timer.elapsed());

    Vector<Result> results;
    struct Foo {
        Node const* ptr;
        EncodedCodePoint cp;
    };
    Queue<Foo> nodes_to_check;
    nodes_to_check.enqueue({ &m_root, EOW });

    while (!nodes_to_check.is_empty()) {
        auto foo = nodes_to_check.dequeue();
        auto node = foo.ptr;
        auto const& data = node->metadata_value();

        if (!data.enabled)
            continue;

        if (data.projected_probability < m_minimum_accepted_probability)
            continue;

        if (data.data.length != 0)
            results.empend(data.data.view(*this), data.projected_probability);

        for (auto& child : node->children()) {
            if (child.key != EOW && child.value->metadata_value().enabled) {
                nodes_to_check.enqueue({ static_cast<Node const*>(child.value.ptr()), child.key });
            }
        }
    }

    // Suggest words with fewer edits.
    // As this produces invalid 'probabilities', we have to scale them back down to 0..1.
    float max_value = 0;
    for (auto& result : results) {
        auto distance = levenshtein_distance(result.suggestion, string);
        float multiplier = static_cast<double>(max(result.suggestion.length(), string.length())) / static_cast<double>(distance);
        result.probability *= multiplier;
        if (result.probability > max_value)
            max_value = result.probability;
    }

    for (auto& result : results)
        result.probability /= max_value;

    quick_sort(results, [](auto const& a, auto const& b) {
        return b.probability < a.probability;
    });

    auto it = results.find_if([this](auto& result) { return result.probability < m_minimum_accepted_probability; });
    if (!it.is_end())
        max_to_fetch = min(max_to_fetch, it.index());

    results.resize(min(max_to_fetch, results.size()));

    return results;
}

}