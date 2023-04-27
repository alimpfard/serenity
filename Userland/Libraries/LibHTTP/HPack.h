/*
 * Copyright (c) 2023, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/DeprecatedString.h>
#include <AK/DistinctNumeric.h>
#include <AK/Forward.h>
#include <LibHTTP/Header.h>

namespace HTTP::HTTP2 {

enum class PseudoHeaderName {
    Method,
    Scheme,
    Authority,
    Path,
    Status,
};

struct PseudoHeader {
    PseudoHeaderName name;
    DeprecatedString value;
};

}

namespace HTTP::HTTP2::HPack {

AK_TYPEDEF_DISTINCT_ORDERED_ID(u32, TableIndex)

struct StaticTableEntry {
    StringView key;
    StringView value;
};

constexpr static auto make_static_table()
{
    return Array {
        StaticTableEntry { ":authority"sv, ""sv },
        StaticTableEntry { ":method"sv, "GET"sv },
        StaticTableEntry { ":method"sv, "POST"sv },
        StaticTableEntry { ":path"sv, "/"sv },
        StaticTableEntry { ":path"sv, "/index.html"sv },
        StaticTableEntry { ":scheme"sv, "http"sv },
        StaticTableEntry { ":scheme"sv, "https"sv },
        StaticTableEntry { ":status"sv, "200"sv },
        StaticTableEntry { ":status"sv, "204"sv },
        StaticTableEntry { ":status"sv, "206"sv },
        StaticTableEntry { ":status"sv, "304"sv },
        StaticTableEntry { ":status"sv, "400"sv },
        StaticTableEntry { ":status"sv, "404"sv },
        StaticTableEntry { ":status"sv, "500"sv },
        StaticTableEntry { "accept-charset"sv, ""sv },
        StaticTableEntry { "accept-encoding"sv, "gzip, deflate"sv },
        StaticTableEntry { "accept-language"sv, ""sv },
        StaticTableEntry { "accept-ranges"sv, ""sv },
        StaticTableEntry { "accept"sv, ""sv },
        StaticTableEntry { "access-control-allow-origin"sv, ""sv },
        StaticTableEntry { "age"sv, ""sv },
        StaticTableEntry { "allow"sv, ""sv },
        StaticTableEntry { "authorization"sv, ""sv },
        StaticTableEntry { "cache-control"sv, ""sv },
        StaticTableEntry { "content-disposition"sv, ""sv },
        StaticTableEntry { "content-encoding"sv, ""sv },
        StaticTableEntry { "content-language"sv, ""sv },
        StaticTableEntry { "content-length"sv, ""sv },
        StaticTableEntry { "content-location"sv, ""sv },
        StaticTableEntry { "content-range"sv, ""sv },
        StaticTableEntry { "content-type"sv, ""sv },
        StaticTableEntry { "cookie"sv, ""sv },
        StaticTableEntry { "date"sv, ""sv },
        StaticTableEntry { "etag"sv, ""sv },
        StaticTableEntry { "expect"sv, ""sv },
        StaticTableEntry { "expires"sv, ""sv },
        StaticTableEntry { "from"sv, ""sv },
        StaticTableEntry { "host"sv, ""sv },
        StaticTableEntry { "if-match"sv, ""sv },
        StaticTableEntry { "if-modified-since"sv, ""sv },
        StaticTableEntry { "if-none-match"sv, ""sv },
        StaticTableEntry { "if-range"sv, ""sv },
        StaticTableEntry { "if-unmodified-since"sv, ""sv },
        StaticTableEntry { "last-modified"sv, ""sv },
        StaticTableEntry { "link"sv, ""sv },
        StaticTableEntry { "location"sv, ""sv },
        StaticTableEntry { "max-forwards"sv, ""sv },
        StaticTableEntry { "proxy-authenticate"sv, ""sv },
        StaticTableEntry { "proxy-authorization"sv, ""sv },
        StaticTableEntry { "range"sv, ""sv },
        StaticTableEntry { "referer"sv, ""sv },
        StaticTableEntry { "refresh"sv, ""sv },
        StaticTableEntry { "retry-after"sv, ""sv },
        StaticTableEntry { "server"sv, ""sv },
        StaticTableEntry { "set-cookie"sv, ""sv },
        StaticTableEntry { "strict-transport-security"sv, ""sv },
        StaticTableEntry { "transfer-encoding"sv, ""sv },
        StaticTableEntry { "user-agent"sv, ""sv },
        StaticTableEntry { "vary"sv, ""sv },
        StaticTableEntry { "via"sv, ""sv },
        StaticTableEntry { "www-authenticate"sv, ""sv },
    };
}

struct StaticTable {
    static Optional<StaticTableEntry> get(TableIndex index)
    {
        if (index.value() >= entries.size())
            return {};
        return entries[index.value()];
    }

    static size_t first_unpopulated_index() { return entries.size(); }

    static Optional<TableIndex> index_of(StringView name, Optional<StringView> value);

private:
    static constexpr auto entries = make_static_table();
};

struct DynamicTableEntry {
    DeprecatedString key;
    DeprecatedString value;
};

struct DynamicTable {
    Vector<NonnullOwnPtr<DynamicTableEntry>> table;
    size_t octet_size { 32 };
    size_t max_octet_size { 4096 };

    void evict_to_fit_size(size_t desired_max_size, bool should_set_max_size = true);

    Optional<DynamicTableEntry const&> get(TableIndex index) const
    {
        if (index < StaticTable::first_unpopulated_index())
            return {};
        return at(index.value() - StaticTable::first_unpopulated_index());
    }

    ErrorOr<void> insert(DynamicTableEntry&& entry);

    Optional<TableIndex> index_of(StringView name, Optional<StringView> value) const;

private:
    DynamicTableEntry const& at(size_t index) const;
};

struct Encoder {
    explicit Encoder(DynamicTable& dynamic_table)
        : m_dynamic_table(dynamic_table)
    {
    }

    ErrorOr<ByteBuffer> encode(Vector<Header> const&, Vector<PseudoHeader> const&);

    static ErrorOr<void> encode_integer(u64 value, u8 flag_bits, u8 prefix_size, ByteBuffer&);
    static ErrorOr<void> encode_string(StringView, bool use_huffman_coding, ByteBuffer&);

private:
    Optional<TableIndex> table_index_of(StringView name, Optional<StringView> value = {}) const;

    DynamicTable& m_dynamic_table;
};

}
