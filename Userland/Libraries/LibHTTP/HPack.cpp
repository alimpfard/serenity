/*
 * Copyright (c) 2023, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/OwnPtr.h>
#include <AK/Vector.h>
#include <LibHTTP/HPack.h>
#include <LibHTTP/Http2Frame.h>

namespace HTTP::HTTP2::HPack {

struct HoffmanEncodingMetrics {
    size_t final_unpadded_bit_length { 0 };
    size_t expected_total_byte_length { 0 };
};

static ErrorOr<ByteBuffer> encode_huffman(StringView string, Optional<HoffmanEncodingMetrics> metrics = {});
static HoffmanEncodingMetrics hoffman_encoded_length(StringView string);

static StringView pseudo_header_name(PseudoHeaderName name)
{
    switch (name) {
    case PseudoHeaderName::Method:
        return ":method"sv;
    case PseudoHeaderName::Scheme:
        return ":scheme"sv;
    case PseudoHeaderName::Authority:
        return ":authority"sv;
    case PseudoHeaderName::Path:
        return ":path"sv;
    case PseudoHeaderName::Status:
        return ":status"sv;
    }
    VERIFY_NOT_REACHED();
}

Optional<TableIndex> StaticTable::index_of(StringView name, Optional<StringView> value)
{
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& entry = entries[i];
        if (entry.key == name && (!value.has_value() || entry.value == value.value()))
            return TableIndex(i);
    }
    return {};
}

DynamicTableEntry const& DynamicTable::at(size_t index) const
{
    return *table[index];
}

void DynamicTable::evict_to_fit_size(size_t desired_max_size, bool should_set_max_size)
{
    if (desired_max_size >= max_octet_size)
        return;

    if (should_set_max_size)
        max_octet_size = desired_max_size;

    if (desired_max_size >= octet_size)
        return;

    while (octet_size > desired_max_size) {
        auto entry = table.take_last();
        octet_size -= entry->key.length() + entry->value.length() + 32;
    }

    VERIFY(octet_size <= max_octet_size);
}

ErrorOr<void> DynamicTable::insert(DynamicTableEntry&& entry)
{
    if (octet_size + entry.key.length() + entry.value.length() + 32 > max_octet_size)
        evict_to_fit_size(max_octet_size - (entry.key.length() + entry.value.length() + 32), false);

    octet_size += entry.key.length() + entry.value.length() + 32;
    table.prepend(make<DynamicTableEntry>(move(entry)));
    return {};
}

Optional<TableIndex> DynamicTable::index_of(StringView name, Optional<StringView> value) const
{
    for (size_t i = 0; i < table.size(); ++i) {
        auto& entry = *table[i];
        if (entry.key == name && (!value.has_value() || entry.value == value.value()))
            return TableIndex(i + StaticTable::first_unpopulated_index() - 1);
    }
    return {};
}

Optional<TableIndex> Encoder::table_index_of(StringView name, Optional<StringView> value) const
{
    if (auto index = StaticTable::index_of(name, value); index.has_value())
        return index;

    if (auto index = m_dynamic_table.index_of(name, value); index.has_value())
        return index;

    return {};
}

ErrorOr<ByteBuffer> Encoder::encode(Vector<Header> const& headers, Vector<PseudoHeader> const& pseudo_headers)
{
    ByteBuffer result_buffer;

    auto encode = [&](StringView name, StringView value) -> ErrorOr<void> {
        // First check if this exists in any of the tables.
        if (auto index = table_index_of(name, value); index.has_value()) {
            // Encode the index using 'Indexed Header Field Representation' (6.1)
            return encode_integer(index->value(), 0b1000'0000, 7, result_buffer);
        }

        // Next, check if only the name exists in a table.
        if (auto index = table_index_of(name); index.has_value()) {
            // Encode the pair using 'Literal Header Field with Incremental Indexing - Indexed Name' (6.2.1)
            TRY(encode_integer(index->value(), 0b0100'0000, 6, result_buffer));
            auto metrics = hoffman_encoded_length(value);
            auto should_use_hoffman = metrics.expected_total_byte_length < value.length();
            TRY(encode_string(value, should_use_hoffman, result_buffer));

            return {};
        }

        // Finally, encode the pair using 'Literal Header Field with Incremental Indexing - New Name' (6.2.1)
        TRY(encode_integer(0, 0b0100'0000, 6, result_buffer));
        auto metrics = hoffman_encoded_length(name);
        auto should_use_hoffman = metrics.expected_total_byte_length < name.length();
        TRY(encode_string(name, should_use_hoffman, result_buffer));

        metrics = hoffman_encoded_length(value);
        should_use_hoffman = metrics.expected_total_byte_length < value.length();
        TRY(encode_string(value, should_use_hoffman, result_buffer));

        return {};
    };

    // First go through the pseudo-headers.
    for (auto const& header : pseudo_headers)
        TRY(encode(pseudo_header_name(header.name), header.value));

    // Then go through the regular headers.
    for (auto const& header : headers) {
        // "cookie" is a special case, we need to split it up by ';'.
        if (header.name.equals_ignoring_ascii_case("cookie"sv)) {
            TRY(header.value.view().for_each_split_view(';', SplitBehavior::Nothing, [&](StringView cookie) -> ErrorOr<void> {
                return encode(header.name, cookie);
            }));
        } else {
            TRY(encode(header.name, header.value));
        }
    }

    return result_buffer;
}

ErrorOr<void> Encoder::encode_integer(u64 value, u8 flag_bits, u8 prefix_size, ByteBuffer& destination)
{
    if (prefix_size < 1 || prefix_size > 8)
        return Error::from_string_literal("Invalid prefix size");

    u8 max_possible_value_for_prefix = (1 << prefix_size) - 1;
    flag_bits = flag_bits & (0xff - max_possible_value_for_prefix);

    if (value < max_possible_value_for_prefix) {
        destination.append(flag_bits | value);
        return {};
    }

    destination.append(flag_bits | max_possible_value_for_prefix);
    value -= max_possible_value_for_prefix;

    constexpr auto continuation_flag = 0x80;
    while (value >= 128) {
        destination.append(continuation_flag | (value & 0x7f));
        value >>= 7;
    }
    destination.append(value);

    return {};
}

struct HuffmanTableEntry {
    u32 code;
    u8 bit_length;
};

constexpr static auto raw_huffman_table = Array {
    HuffmanTableEntry { 0x1ff8, 13 },     // 0 (no spec comment)
    HuffmanTableEntry { 0x7fffd8, 23 },   // 1 (no spec comment)
    HuffmanTableEntry { 0xfffffe2, 28 },  // 2 (no spec comment)
    HuffmanTableEntry { 0xfffffe3, 28 },  // 3 (no spec comment)
    HuffmanTableEntry { 0xfffffe4, 28 },  // 4 (no spec comment)
    HuffmanTableEntry { 0xfffffe5, 28 },  // 5 (no spec comment)
    HuffmanTableEntry { 0xfffffe6, 28 },  // 6 (no spec comment)
    HuffmanTableEntry { 0xfffffe7, 28 },  // 7 (no spec comment)
    HuffmanTableEntry { 0xfffffe8, 28 },  // 8 (no spec comment)
    HuffmanTableEntry { 0xffffea, 24 },   // 9 (no spec comment)
    HuffmanTableEntry { 0x3ffffffc, 30 }, // 10 (no spec comment)
    HuffmanTableEntry { 0xfffffe9, 28 },  // 11 (no spec comment)
    HuffmanTableEntry { 0xfffffea, 28 },  // 12 (no spec comment)
    HuffmanTableEntry { 0x3ffffffd, 30 }, // 13 (no spec comment)
    HuffmanTableEntry { 0xfffffeb, 28 },  // 14 (no spec comment)
    HuffmanTableEntry { 0xfffffec, 28 },  // 15 (no spec comment)
    HuffmanTableEntry { 0xfffffed, 28 },  // 16 (no spec comment)
    HuffmanTableEntry { 0xfffffee, 28 },  // 17 (no spec comment)
    HuffmanTableEntry { 0xfffffef, 28 },  // 18 (no spec comment)
    HuffmanTableEntry { 0xffffff0, 28 },  // 19 (no spec comment)
    HuffmanTableEntry { 0xffffff1, 28 },  // 20 (no spec comment)
    HuffmanTableEntry { 0xffffff2, 28 },  // 21 (no spec comment)
    HuffmanTableEntry { 0x3ffffffe, 30 }, // 22 (no spec comment)
    HuffmanTableEntry { 0xffffff3, 28 },  // 23 (no spec comment)
    HuffmanTableEntry { 0xffffff4, 28 },  // 24 (no spec comment)
    HuffmanTableEntry { 0xffffff5, 28 },  // 25 (no spec comment)
    HuffmanTableEntry { 0xffffff6, 28 },  // 26 (no spec comment)
    HuffmanTableEntry { 0xffffff7, 28 },  // 27 (no spec comment)
    HuffmanTableEntry { 0xffffff8, 28 },  // 28 (no spec comment)
    HuffmanTableEntry { 0xffffff9, 28 },  // 29 (no spec comment)
    HuffmanTableEntry { 0xffffffa, 28 },  // 30 (no spec comment)
    HuffmanTableEntry { 0xffffffb, 28 },  // 31 (no spec comment)
    HuffmanTableEntry { 0x14, 6 },        // 32 (' ')
    HuffmanTableEntry { 0x3f8, 10 },      // 33 ('!')
    HuffmanTableEntry { 0x3f9, 10 },      // 34 ('"')
    HuffmanTableEntry { 0xffa, 12 },      // 35 ('#')
    HuffmanTableEntry { 0x1ff9, 13 },     // 36 ('$')
    HuffmanTableEntry { 0x15, 6 },        // 37 ('%')
    HuffmanTableEntry { 0xf8, 8 },        // 38 ('&')
    HuffmanTableEntry { 0x7fa, 11 },      // 39 (''')
    HuffmanTableEntry { 0x3fa, 10 },      // 40 ('(')
    HuffmanTableEntry { 0x3fb, 10 },      // 41 (')')
    HuffmanTableEntry { 0xf9, 8 },        // 42 ('*')
    HuffmanTableEntry { 0x7fb, 11 },      // 43 ('+')
    HuffmanTableEntry { 0xfa, 8 },        // 44 (',')
    HuffmanTableEntry { 0x16, 6 },        // 45 ('-')
    HuffmanTableEntry { 0x17, 6 },        // 46 ('.')
    HuffmanTableEntry { 0x18, 6 },        // 47 ('/')
    HuffmanTableEntry { 0x0, 5 },         // 48 ('0')
    HuffmanTableEntry { 0x1, 5 },         // 49 ('1')
    HuffmanTableEntry { 0x2, 5 },         // 50 ('2')
    HuffmanTableEntry { 0x19, 6 },        // 51 ('3')
    HuffmanTableEntry { 0x1a, 6 },        // 52 ('4')
    HuffmanTableEntry { 0x1b, 6 },        // 53 ('5')
    HuffmanTableEntry { 0x1c, 6 },        // 54 ('6')
    HuffmanTableEntry { 0x1d, 6 },        // 55 ('7')
    HuffmanTableEntry { 0x1e, 6 },        // 56 ('8')
    HuffmanTableEntry { 0x1f, 6 },        // 57 ('9')
    HuffmanTableEntry { 0x5c, 7 },        // 58 (':')
    HuffmanTableEntry { 0xfb, 8 },        // 59 (';')
    HuffmanTableEntry { 0x7ffc, 15 },     // 60 ('<')
    HuffmanTableEntry { 0x20, 6 },        // 61 ('=')
    HuffmanTableEntry { 0xffb, 12 },      // 62 ('>')
    HuffmanTableEntry { 0x3fc, 10 },      // 63 ('?')
    HuffmanTableEntry { 0x1ffa, 13 },     // 64 ('@')
    HuffmanTableEntry { 0x21, 6 },        // 65 ('A')
    HuffmanTableEntry { 0x5d, 7 },        // 66 ('B')
    HuffmanTableEntry { 0x5e, 7 },        // 67 ('C')
    HuffmanTableEntry { 0x5f, 7 },        // 68 ('D')
    HuffmanTableEntry { 0x60, 7 },        // 69 ('E')
    HuffmanTableEntry { 0x61, 7 },        // 70 ('F')
    HuffmanTableEntry { 0x62, 7 },        // 71 ('G')
    HuffmanTableEntry { 0x63, 7 },        // 72 ('H')
    HuffmanTableEntry { 0x64, 7 },        // 73 ('I')
    HuffmanTableEntry { 0x65, 7 },        // 74 ('J')
    HuffmanTableEntry { 0x66, 7 },        // 75 ('K')
    HuffmanTableEntry { 0x67, 7 },        // 76 ('L')
    HuffmanTableEntry { 0x68, 7 },        // 77 ('M')
    HuffmanTableEntry { 0x69, 7 },        // 78 ('N')
    HuffmanTableEntry { 0x6a, 7 },        // 79 ('O')
    HuffmanTableEntry { 0x6b, 7 },        // 80 ('P')
    HuffmanTableEntry { 0x6c, 7 },        // 81 ('Q')
    HuffmanTableEntry { 0x6d, 7 },        // 82 ('R')
    HuffmanTableEntry { 0x6e, 7 },        // 83 ('S')
    HuffmanTableEntry { 0x6f, 7 },        // 84 ('T')
    HuffmanTableEntry { 0x70, 7 },        // 85 ('U')
    HuffmanTableEntry { 0x71, 7 },        // 86 ('V')
    HuffmanTableEntry { 0x72, 7 },        // 87 ('W')
    HuffmanTableEntry { 0xfc, 8 },        // 88 ('X')
    HuffmanTableEntry { 0x73, 7 },        // 89 ('Y')
    HuffmanTableEntry { 0xfd, 8 },        // 90 ('Z')
    HuffmanTableEntry { 0x1ffb, 13 },     // 91 ('[')
    HuffmanTableEntry { 0x7fff0, 19 },    // 92 ('\')
    HuffmanTableEntry { 0x1ffc, 13 },     // 93 (']')
    HuffmanTableEntry { 0x3ffc, 14 },     // 94 ('^')
    HuffmanTableEntry { 0x22, 6 },        // 95 ('_')
    HuffmanTableEntry { 0x7ffd, 15 },     // 96 ('`')
    HuffmanTableEntry { 0x3, 5 },         // 97 ('a')
    HuffmanTableEntry { 0x23, 6 },        // 98 ('b')
    HuffmanTableEntry { 0x4, 5 },         // 99 ('c')
    HuffmanTableEntry { 0x24, 6 },        // 100 ('d')
    HuffmanTableEntry { 0x5, 5 },         // 101 ('e')
    HuffmanTableEntry { 0x25, 6 },        // 102 ('f')
    HuffmanTableEntry { 0x26, 6 },        // 103 ('g')
    HuffmanTableEntry { 0x27, 6 },        // 104 ('h')
    HuffmanTableEntry { 0x6, 5 },         // 105 ('i')
    HuffmanTableEntry { 0x74, 7 },        // 106 ('j')
    HuffmanTableEntry { 0x75, 7 },        // 107 ('k')
    HuffmanTableEntry { 0x28, 6 },        // 108 ('l')
    HuffmanTableEntry { 0x29, 6 },        // 109 ('m')
    HuffmanTableEntry { 0x2a, 6 },        // 110 ('n')
    HuffmanTableEntry { 0x7, 5 },         // 111 ('o')
    HuffmanTableEntry { 0x2b, 6 },        // 112 ('p')
    HuffmanTableEntry { 0x76, 7 },        // 113 ('q')
    HuffmanTableEntry { 0x2c, 6 },        // 114 ('r')
    HuffmanTableEntry { 0x8, 5 },         // 115 ('s')
    HuffmanTableEntry { 0x9, 5 },         // 116 ('t')
    HuffmanTableEntry { 0x2d, 6 },        // 117 ('u')
    HuffmanTableEntry { 0x77, 7 },        // 118 ('v')
    HuffmanTableEntry { 0x78, 7 },        // 119 ('w')
    HuffmanTableEntry { 0x79, 7 },        // 120 ('x')
    HuffmanTableEntry { 0x7a, 7 },        // 121 ('y')
    HuffmanTableEntry { 0x7b, 7 },        // 122 ('z')
    HuffmanTableEntry { 0x7ffe, 15 },     // 123 ('{')
    HuffmanTableEntry { 0x7fc, 11 },      // 124 ('|')
    HuffmanTableEntry { 0x3ffd, 14 },     // 125 ('}')
    HuffmanTableEntry { 0x1ffd, 13 },     // 126 ('~')
    HuffmanTableEntry { 0xffffffc, 28 },  // 127 (no spec comment)
    HuffmanTableEntry { 0xfffe6, 20 },    // 128 (no spec comment)
    HuffmanTableEntry { 0x3fffd2, 22 },   // 129 (no spec comment)
    HuffmanTableEntry { 0xfffe7, 20 },    // 130 (no spec comment)
    HuffmanTableEntry { 0xfffe8, 20 },    // 131 (no spec comment)
    HuffmanTableEntry { 0x3fffd3, 22 },   // 132 (no spec comment)
    HuffmanTableEntry { 0x3fffd4, 22 },   // 133 (no spec comment)
    HuffmanTableEntry { 0x3fffd5, 22 },   // 134 (no spec comment)
    HuffmanTableEntry { 0x7fffd9, 23 },   // 135 (no spec comment)
    HuffmanTableEntry { 0x3fffd6, 22 },   // 136 (no spec comment)
    HuffmanTableEntry { 0x7fffda, 23 },   // 137 (no spec comment)
    HuffmanTableEntry { 0x7fffdb, 23 },   // 138 (no spec comment)
    HuffmanTableEntry { 0x7fffdc, 23 },   // 139 (no spec comment)
    HuffmanTableEntry { 0x7fffdd, 23 },   // 140 (no spec comment)
    HuffmanTableEntry { 0x7fffde, 23 },   // 141 (no spec comment)
    HuffmanTableEntry { 0xffffeb, 24 },   // 142 (no spec comment)
    HuffmanTableEntry { 0x7fffdf, 23 },   // 143 (no spec comment)
    HuffmanTableEntry { 0xffffec, 24 },   // 144 (no spec comment)
    HuffmanTableEntry { 0xffffed, 24 },   // 145 (no spec comment)
    HuffmanTableEntry { 0x3fffd7, 22 },   // 146 (no spec comment)
    HuffmanTableEntry { 0x7fffe0, 23 },   // 147 (no spec comment)
    HuffmanTableEntry { 0xffffee, 24 },   // 148 (no spec comment)
    HuffmanTableEntry { 0x7fffe1, 23 },   // 149 (no spec comment)
    HuffmanTableEntry { 0x7fffe2, 23 },   // 150 (no spec comment)
    HuffmanTableEntry { 0x7fffe3, 23 },   // 151 (no spec comment)
    HuffmanTableEntry { 0x7fffe4, 23 },   // 152 (no spec comment)
    HuffmanTableEntry { 0x1fffdc, 21 },   // 153 (no spec comment)
    HuffmanTableEntry { 0x3fffd8, 22 },   // 154 (no spec comment)
    HuffmanTableEntry { 0x7fffe5, 23 },   // 155 (no spec comment)
    HuffmanTableEntry { 0x3fffd9, 22 },   // 156 (no spec comment)
    HuffmanTableEntry { 0x7fffe6, 23 },   // 157 (no spec comment)
    HuffmanTableEntry { 0x7fffe7, 23 },   // 158 (no spec comment)
    HuffmanTableEntry { 0xffffef, 24 },   // 159 (no spec comment)
    HuffmanTableEntry { 0x3fffda, 22 },   // 160 (no spec comment)
    HuffmanTableEntry { 0x1fffdd, 21 },   // 161 (no spec comment)
    HuffmanTableEntry { 0xfffe9, 20 },    // 162 (no spec comment)
    HuffmanTableEntry { 0x3fffdb, 22 },   // 163 (no spec comment)
    HuffmanTableEntry { 0x3fffdc, 22 },   // 164 (no spec comment)
    HuffmanTableEntry { 0x7fffe8, 23 },   // 165 (no spec comment)
    HuffmanTableEntry { 0x7fffe9, 23 },   // 166 (no spec comment)
    HuffmanTableEntry { 0x1fffde, 21 },   // 167 (no spec comment)
    HuffmanTableEntry { 0x7fffea, 23 },   // 168 (no spec comment)
    HuffmanTableEntry { 0x3fffdd, 22 },   // 169 (no spec comment)
    HuffmanTableEntry { 0x3fffde, 22 },   // 170 (no spec comment)
    HuffmanTableEntry { 0xfffff0, 24 },   // 171 (no spec comment)
    HuffmanTableEntry { 0x1fffdf, 21 },   // 172 (no spec comment)
    HuffmanTableEntry { 0x3fffdf, 22 },   // 173 (no spec comment)
    HuffmanTableEntry { 0x7fffeb, 23 },   // 174 (no spec comment)
    HuffmanTableEntry { 0x7fffec, 23 },   // 175 (no spec comment)
    HuffmanTableEntry { 0x1fffe0, 21 },   // 176 (no spec comment)
    HuffmanTableEntry { 0x1fffe1, 21 },   // 177 (no spec comment)
    HuffmanTableEntry { 0x3fffe0, 22 },   // 178 (no spec comment)
    HuffmanTableEntry { 0x1fffe2, 21 },   // 179 (no spec comment)
    HuffmanTableEntry { 0x7fffed, 23 },   // 180 (no spec comment)
    HuffmanTableEntry { 0x3fffe1, 22 },   // 181 (no spec comment)
    HuffmanTableEntry { 0x7fffee, 23 },   // 182 (no spec comment)
    HuffmanTableEntry { 0x7fffef, 23 },   // 183 (no spec comment)
    HuffmanTableEntry { 0xfffea, 20 },    // 184 (no spec comment)
    HuffmanTableEntry { 0x3fffe2, 22 },   // 185 (no spec comment)
    HuffmanTableEntry { 0x3fffe3, 22 },   // 186 (no spec comment)
    HuffmanTableEntry { 0x3fffe4, 22 },   // 187 (no spec comment)
    HuffmanTableEntry { 0x7ffff0, 23 },   // 188 (no spec comment)
    HuffmanTableEntry { 0x3fffe5, 22 },   // 189 (no spec comment)
    HuffmanTableEntry { 0x3fffe6, 22 },   // 190 (no spec comment)
    HuffmanTableEntry { 0x7ffff1, 23 },   // 191 (no spec comment)
    HuffmanTableEntry { 0x3ffffe0, 26 },  // 192 (no spec comment)
    HuffmanTableEntry { 0x3ffffe1, 26 },  // 193 (no spec comment)
    HuffmanTableEntry { 0xfffeb, 20 },    // 194 (no spec comment)
    HuffmanTableEntry { 0x7fff1, 19 },    // 195 (no spec comment)
    HuffmanTableEntry { 0x3fffe7, 22 },   // 196 (no spec comment)
    HuffmanTableEntry { 0x7ffff2, 23 },   // 197 (no spec comment)
    HuffmanTableEntry { 0x3fffe8, 22 },   // 198 (no spec comment)
    HuffmanTableEntry { 0x1ffffec, 25 },  // 199 (no spec comment)
    HuffmanTableEntry { 0x3ffffe2, 26 },  // 200 (no spec comment)
    HuffmanTableEntry { 0x3ffffe3, 26 },  // 201 (no spec comment)
    HuffmanTableEntry { 0x3ffffe4, 26 },  // 202 (no spec comment)
    HuffmanTableEntry { 0x7ffffde, 27 },  // 203 (no spec comment)
    HuffmanTableEntry { 0x7ffffdf, 27 },  // 204 (no spec comment)
    HuffmanTableEntry { 0x3ffffe5, 26 },  // 205 (no spec comment)
    HuffmanTableEntry { 0xfffff1, 24 },   // 206 (no spec comment)
    HuffmanTableEntry { 0x1ffffed, 25 },  // 207 (no spec comment)
    HuffmanTableEntry { 0x7fff2, 19 },    // 208 (no spec comment)
    HuffmanTableEntry { 0x1fffe3, 21 },   // 209 (no spec comment)
    HuffmanTableEntry { 0x3ffffe6, 26 },  // 210 (no spec comment)
    HuffmanTableEntry { 0x7ffffe0, 27 },  // 211 (no spec comment)
    HuffmanTableEntry { 0x7ffffe1, 27 },  // 212 (no spec comment)
    HuffmanTableEntry { 0x3ffffe7, 26 },  // 213 (no spec comment)
    HuffmanTableEntry { 0x7ffffe2, 27 },  // 214 (no spec comment)
    HuffmanTableEntry { 0xfffff2, 24 },   // 215 (no spec comment)
    HuffmanTableEntry { 0x1fffe4, 21 },   // 216 (no spec comment)
    HuffmanTableEntry { 0x1fffe5, 21 },   // 217 (no spec comment)
    HuffmanTableEntry { 0x3ffffe8, 26 },  // 218 (no spec comment)
    HuffmanTableEntry { 0x3ffffe9, 26 },  // 219 (no spec comment)
    HuffmanTableEntry { 0xffffffd, 28 },  // 220 (no spec comment)
    HuffmanTableEntry { 0x7ffffe3, 27 },  // 221 (no spec comment)
    HuffmanTableEntry { 0x7ffffe4, 27 },  // 222 (no spec comment)
    HuffmanTableEntry { 0x7ffffe5, 27 },  // 223 (no spec comment)
    HuffmanTableEntry { 0xfffec, 20 },    // 224 (no spec comment)
    HuffmanTableEntry { 0xfffff3, 24 },   // 225 (no spec comment)
    HuffmanTableEntry { 0xfffed, 20 },    // 226 (no spec comment)
    HuffmanTableEntry { 0x1fffe6, 21 },   // 227 (no spec comment)
    HuffmanTableEntry { 0x3fffe9, 22 },   // 228 (no spec comment)
    HuffmanTableEntry { 0x1fffe7, 21 },   // 229 (no spec comment)
    HuffmanTableEntry { 0x1fffe8, 21 },   // 230 (no spec comment)
    HuffmanTableEntry { 0x7ffff3, 23 },   // 231 (no spec comment)
    HuffmanTableEntry { 0x3fffea, 22 },   // 232 (no spec comment)
    HuffmanTableEntry { 0x3fffeb, 22 },   // 233 (no spec comment)
    HuffmanTableEntry { 0x1ffffee, 25 },  // 234 (no spec comment)
    HuffmanTableEntry { 0x1ffffef, 25 },  // 235 (no spec comment)
    HuffmanTableEntry { 0xfffff4, 24 },   // 236 (no spec comment)
    HuffmanTableEntry { 0xfffff5, 24 },   // 237 (no spec comment)
    HuffmanTableEntry { 0x3ffffea, 26 },  // 238 (no spec comment)
    HuffmanTableEntry { 0x7ffff4, 23 },   // 239 (no spec comment)
    HuffmanTableEntry { 0x3ffffeb, 26 },  // 240 (no spec comment)
    HuffmanTableEntry { 0x7ffffe6, 27 },  // 241 (no spec comment)
    HuffmanTableEntry { 0x3ffffec, 26 },  // 242 (no spec comment)
    HuffmanTableEntry { 0x3ffffed, 26 },  // 243 (no spec comment)
    HuffmanTableEntry { 0x7ffffe7, 27 },  // 244 (no spec comment)
    HuffmanTableEntry { 0x7ffffe8, 27 },  // 245 (no spec comment)
    HuffmanTableEntry { 0x7ffffe9, 27 },  // 246 (no spec comment)
    HuffmanTableEntry { 0x7ffffea, 27 },  // 247 (no spec comment)
    HuffmanTableEntry { 0x7ffffeb, 27 },  // 248 (no spec comment)
    HuffmanTableEntry { 0xffffffe, 28 },  // 249 (no spec comment)
    HuffmanTableEntry { 0x7ffffec, 27 },  // 250 (no spec comment)
    HuffmanTableEntry { 0x7ffffed, 27 },  // 251 (no spec comment)
    HuffmanTableEntry { 0x7ffffee, 27 },  // 252 (no spec comment)
    HuffmanTableEntry { 0x7ffffef, 27 },  // 253 (no spec comment)
    HuffmanTableEntry { 0x7fffff0, 27 },  // 254 (no spec comment)
    HuffmanTableEntry { 0x3ffffee, 26 },  // 255 (no spec comment)
    HuffmanTableEntry { 0x3fffffff, 30 }, // 256 (EOS)
};

HoffmanEncodingMetrics hoffman_encoded_length(StringView string)
{
    HoffmanEncodingMetrics metrics {
        .final_unpadded_bit_length = 0,
        .expected_total_byte_length = 0,
    };

    for (auto ch : string)
        metrics.final_unpadded_bit_length += raw_huffman_table[ch].bit_length;

    // Final byte padded with EOS (all 1s)
    metrics.expected_total_byte_length = (metrics.final_unpadded_bit_length + 7) / 8;

    return metrics;
}

ErrorOr<ByteBuffer> encode_huffman(StringView string, Optional<HoffmanEncodingMetrics> metrics)
{
    ByteBuffer buffer;

    auto [final_unpadded_bit_length, expected_total_byte_length] = metrics.value_or_lazy_evaluated([&] { return hoffman_encoded_length(string); });

    TRY(buffer.try_resize(expected_total_byte_length));

    auto span = buffer.bytes();
    size_t bit_offset = 0;

    for (auto ch : string) {
        auto entry = raw_huffman_table[ch];
        auto bits = entry.code;
        auto bit_length = entry.bit_length;

        while (bit_length > 0) {
            auto byte_offset = bit_offset / 8;
            auto bit_in_byte_offset = bit_offset % 8;
            auto bits_to_write = min(bit_length, 8 - bit_in_byte_offset);
            auto bits_to_write_shifted = (bits & ((1 << bits_to_write) - 1)) << bit_in_byte_offset;
            span[byte_offset] |= bits_to_write_shifted;
            bits >>= bits_to_write;
            bit_length -= bits_to_write;
            bit_offset += bits_to_write;
        }
    }

    // Final byte padded with EOS (all 1s)
    auto final_byte_offset = bit_offset / 8;
    auto final_bit_in_byte_offset = bit_offset % 8;
    auto final_bits_to_write = 8 - final_bit_in_byte_offset;
    auto final_bits_to_write_shifted = ((1 << final_bits_to_write) - 1) << final_bit_in_byte_offset;
    span[final_byte_offset] |= final_bits_to_write_shifted;

    VERIFY(bit_offset == final_unpadded_bit_length);

    return buffer;
}

ErrorOr<void> Encoder::encode_string(StringView string, bool use_huffman_coding, ByteBuffer& destination)
{
    u8 flags = 0;
    ByteBuffer bytes;

    if (use_huffman_coding) {
        flags = 0x1; // Huffman encoded
        bytes = TRY(encode_huffman(string));
    } else {
        bytes = TRY(ByteBuffer::copy(string.bytes()));
    }

    TRY(encode_integer(bytes.size(), flags, 7, destination));
    TRY(destination.try_append(bytes));

    return {};
}

}
