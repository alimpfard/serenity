/*
 * Copyright (c) 2023, Liav A. <liavalb@hotmail.co.il>
 * Copyright (c) 2023, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#undef KERNEL

#include <AK/ByteReader.h>
#include <AK/Error.h>
#include <AK/Format.h>
#include <AK/String.h>
#include <AK/StringData.h>

namespace AK {

Error Error::from_string_view_or_print_error_and_return_errno(StringView string_literal, [[maybe_unused]] int code)
{
#ifdef KERNEL
    dmesgln("{}", string_literal);
    return Error::from_errno(code);
#else
    return Error::from_string_view(string_literal);
#endif
}

#ifndef KERNEL
size_t Error::pack_string(u8* buffer, size_t offset, AK::String const& string)
{
    if (string.is_short_string()) {
        buffer[offset++] = to_underlying(FormattedString::Type::ShortStringImpl);
        auto short_string = string.short_string_data({});
        static_assert(sizeof(short_string) <= sizeof(FlatPtr), "short_string_data() should return a pointer-sized value");
        u8 temp_buffer[sizeof(FlatPtr)] { 0 };
        __builtin_memcpy(temp_buffer, &short_string, sizeof(short_string));
        __builtin_memcpy(&buffer[offset], temp_buffer, sizeof(temp_buffer));
        return offset + sizeof(temp_buffer);
    }

    auto string_data_ptr = &string.string_data({});
    string_data_ptr->ref();
    buffer[offset++] = to_underlying(FormattedString::Type::StringImpl);
    __builtin_memcpy(&buffer[offset], &string_data_ptr, sizeof(FlatPtr));
    return offset + sizeof(FlatPtr);
}

Error::~Error()
{
    if (m_data.has<FormattedString>()) [[unlikely]] {
        auto& format_data = m_data.get<FormattedString>();
        size_t offset = 0;
        for (; format_data.buffer[offset] != 0;) {
            auto type = static_cast<FormattedString::Type>(format_data.buffer[offset]);
            offset += 1;
            switch (type) {
            case FormattedString::Type::Nothing:
                VERIFY_NOT_REACHED();

#    define IGNORE_CASE(Name, T)      \
    case FormattedString::Type::Name: \
        offset += sizeof(T);          \
        break

                IGNORE_CASE(StringView, StringView);
                IGNORE_CASE(ShortStringImpl, FlatPtr);
                IGNORE_CASE(U8, u8);
                IGNORE_CASE(U16, u16);
                IGNORE_CASE(U32, u32);
                IGNORE_CASE(U64, u64);
                IGNORE_CASE(I8, i8);
                IGNORE_CASE(I16, i16);
                IGNORE_CASE(I32, i32);
                IGNORE_CASE(I64, i64);

#    undef CASE

            case FormattedString::Type::StringImpl: {
                ByteReader::load_pointer<Detail::StringData>(&format_data.buffer[offset])->unref();
                offset += sizeof(FlatPtr);
                break;
            }
            }
        }
    }
}

template<typename R>
R Error::format_impl() const
{
    static_assert(IsSame<R, ErrorOr<AK::String>>);

    auto& format_data = m_data.get<FormattedString>();
    struct FormatParams : TypeErasedFormatParams {
        FormatParams()
            : TypeErasedFormatParams(0)
        {
        }

        void set_size(u32 size)
        {
            *reinterpret_cast<u32*>(this) = size;
        }

        alignas(AK::TypeErasedParameter) u8 bits[sizeof(AK::TypeErasedParameter) * ((64 - sizeof(StringView)) / 2)];
    } params_storage;
    auto* params = bit_cast<AK::TypeErasedParameter*>(&params_storage.bits[0]);
    size_t count = 0;
    size_t offset = 0;

    u8 aligned_local_storage[1 * KiB] {};
    size_t aligned_local_storage_offset = 0;
    auto allocate_aligned_on_local_storage = [&]<typename T>(u8 const* p) {
        auto start_offset = align_up_to(aligned_local_storage_offset, alignof(T));
        VERIFY(array_size(aligned_local_storage) >= start_offset + sizeof(T));
        __builtin_memcpy(&aligned_local_storage[start_offset], p, sizeof(T));
        aligned_local_storage_offset = start_offset + sizeof(T);
        return bit_cast<T const*>(&aligned_local_storage[start_offset]);
    };

    for (; format_data.buffer[offset] != 0;) {
        auto type = static_cast<FormattedString::Type>(format_data.buffer[offset]);
        offset += 1;
        switch (type) {
        case FormattedString::Type::Nothing:
            VERIFY_NOT_REACHED();

#    define CASE(Name, T)                                                                          \
    case FormattedString::Type::Name:                                                              \
        params[count++] = AK::TypeErasedParameter {                                                \
            allocate_aligned_on_local_storage.template operator()<T>(&format_data.buffer[offset]), \
            AK::TypeErasedParameter::get_type<T>(),                                                \
            AK::__format_value<T>                                                                  \
        };                                                                                         \
        offset += sizeof(T);                                                                       \
        break

            CASE(StringView, StringView);
            CASE(U8, u8);
            CASE(U16, u16);
            CASE(U32, u32);
            CASE(U64, u64);
            CASE(I8, i8);
            CASE(I16, i16);
            CASE(I32, i32);
            CASE(I64, i64);

#    undef CASE

        case FormattedString::Type::StringImpl:
        case FormattedString::Type::ShortStringImpl:
            params[count++] = AK::TypeErasedParameter {
                allocate_aligned_on_local_storage.template operator()<AK::String>(&format_data.buffer[offset]),
                AK::TypeErasedParameter::get_type<AK::String>(),
                AK::__format_value<AK::String>
            };
            offset += sizeof(FlatPtr);
            break;
        }
    }

    params_storage.set_size(count);
    return AK::String::vformatted(format_data.format_string, params_storage);
}

template ErrorOr<String> Error::format_impl<ErrorOr<String>>() const;
#endif

}
