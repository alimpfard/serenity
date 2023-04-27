/*
 * Copyright (c) 2023, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/DistinctNumeric.h>
#include <AK/EnumBits.h>
#include <AK/Forward.h>
#include <LibHTTP/HPack.h>

namespace HTTP::HTTP2 {

AK_TYPEDEF_DISTINCT_ORDERED_ID(u32, StreamId)
struct Stream {
    StreamId id;
    HPack::Encoder header_encoder() { return HPack::Encoder(m_dynamic_hpack_table); }

private:
    HPack::DynamicTable m_dynamic_hpack_table;
};

class Http2Frame {
public:
    struct Data {
        static constexpr u8 type = 0x0;

        ByteBuffer data;

        enum class Flags : u8 {
            None = 0,
            EndStream = 0x1,
        };

        AK_ENUM_BITWISE_FRIEND_OPERATORS(Flags);
    };
    struct Headers {
        static constexpr u8 type = 0x1;

        bool exclusive_dependency { false };
        StreamId stream_dependency { 0 };
        u8 weight { 0 };
        ByteBuffer block_fragment;

        enum class Flags : u8 {
            None = 0,
            EndStream = 0x1,
            EndHeaders = 0x4,
            Padded = 0x8,
            Priority = 0x20,
        };

        AK_ENUM_BITWISE_FRIEND_OPERATORS(Flags);
    };
    struct Priority {
        static constexpr u8 type = 0x2;

        bool exclusive_dependency { false };
        StreamId stream_dependency { 0 };
        u8 weight { 0 };

        enum class Flags : u8 {
            None = 0,
        };

        AK_ENUM_BITWISE_FRIEND_OPERATORS(Flags);
    };
    struct RstStream {
        static constexpr u8 type = 0x3;

        u32 error_code { 0 };

        enum class Flags : u8 {
            None = 0,
        };

        AK_ENUM_BITWISE_FRIEND_OPERATORS(Flags);
    };
    struct Settings {
        static constexpr u8 type = 0x4;

        struct Pair {
            u16 identifier;
            u32 value;
        };

        Vector<Pair> settings;

        enum class Flags : u8 {
            None = 0,
            Ack = 0x1,
        };

        AK_ENUM_BITWISE_FRIEND_OPERATORS(Flags);

        enum class Identifiers : u8 {
            HeaderTableSize = 0x1,      // SETTINGS_HEADER_TABLE_SIZE
            EnablePush = 0x2,           // SETTINGS_ENABLE_PUSH
            MaxConcurrentStreams = 0x3, // SETTINGS_MAX_CONCURRENT_STREAMS
            InitialWindowSize = 0x4,    // SETTINGS_INITIAL_WINDOW_SIZE
            MaxFrameSize = 0x5,         // SETTINGS_MAX_FRAME_SIZE
            MaxHeaderListSize = 0x6,    // SETTINGS_MAX_HEADER_LIST_SIZE
        };
    };
    struct PushPromise {
        static constexpr u8 type = 0x5;

        StreamId promised_stream_id { 0 }; // First bit is reserved, must be zero.
        ByteBuffer header_block_fragment;

        enum class Flags : u8 {
            None = 0,
            EndHeaders = 0x4,
            Padded = 0x8,
        };

        AK_ENUM_BITWISE_FRIEND_OPERATORS(Flags);
    };
    struct Ping {
        static constexpr u8 type = 0x6;

        u64 opaque_data { 0 };

        enum class Flags : u8 {
            None = 0,
            Ack = 0x1,
        };

        AK_ENUM_BITWISE_FRIEND_OPERATORS(Flags);
    };
    struct GoAway {
        static constexpr u8 type = 0x7;

        StreamId last_stream_id { 0 }; // First bit is reserved, must be zero.
        u32 error_code { 0 };
        ByteBuffer additional_debug_data;

        enum class Flags : u8 {
            None = 0,
        };

        AK_ENUM_BITWISE_FRIEND_OPERATORS(Flags);
    };
    struct WindowUpdate {
        static constexpr u8 type = 0x8;

        u32 window_size_increment { 0 }; // First bit is reserved, must be zero.

        enum class Flags : u8 {
            None = 0,
        };

        AK_ENUM_BITWISE_FRIEND_OPERATORS(Flags);
    };

    enum class Type : u8 {
        Data = Data::type,
        Headers = Headers::type,
        Priority = Priority::type,
        RstStream = RstStream::type,
        Settings = Settings::type,
        PushPromise = PushPromise::type,
        Ping = Ping::type,
        GoAway = GoAway::type,
        WindowUpdate = WindowUpdate::type,
    };
    using Payload = Variant<Data, Headers, Priority, RstStream, Settings, PushPromise, Ping, GoAway, WindowUpdate>;

    template<typename PayloadType>
    requires(Payload::can_contain<PayloadType>())
    Http2Frame(Stream& stream, typename PayloadType::Flags flags, PayloadType&& payload)
        : m_type(static_cast<Type>(PayloadType::type))
        , m_flags(to_underlying(flags))
        , m_stream_id(stream.id)
        , m_payload(forward<PayloadType>(payload))
    {
    }

    Type type() const { return m_type; }
    u8 flags() const { return m_flags; }
    StreamId stream_id() const { return m_stream_id; }
    Payload const& payload() const { return m_payload; }

private:
    Type m_type { 0 };
    u8 m_flags { 0 };
    StreamId m_stream_id { 0 };
    Payload m_payload;
};

}

namespace HTTP {

using HTTP2::Http2Frame;

}
