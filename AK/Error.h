/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/CheckedFormatString.h>
#include <AK/StringView.h>
#include <AK/Try.h>
#include <AK/Variant.h>

#if defined(AK_OS_SERENITY) && defined(KERNEL)
#    include <errno_codes.h>
#else
#    include <errno.h>
#    include <string.h>
#endif

namespace AK {
namespace Detail {
template<typename T>
constexpr bool CanBePlacedInErrorFormattedStringBuffer = IsOneOf<T, u8, u16, u32, u64, i8, i16, i32, i64, StringView, AK::String>;

template<typename T>
consteval size_t formatted_size_of()
{
    if constexpr (IsSame<T, AK::String>)
        return sizeof(FlatPtr);
    return sizeof(T);
}

template<typename... Ts>
consteval size_t error_formatted_string_buffer_encoded_size()
{
    return ((formatted_size_of<Ts>() + 1) + ...);
}
}

/// Format Buffer Encoding:
///   Type ::= u8 { Nothing{s=0}, U<n>{s=n/8}, I<n>{s=n/8}, StringView{s=sizeof(StringView)}, StringImpl{s=sizeof(FlatPtr)} } where n ::= { 8, 16, 32, 64 }
///   FormatBufferEntry ::= Type [u8 * Type.s]
///   FormatBuffer ::= FormatBufferEntry Type::Nothing
template<typename FormatBuffer, typename... Ts>
constexpr bool FitsInErrorFormattedStringBuffer = requires {
    requires(Detail::CanBePlacedInErrorFormattedStringBuffer<Ts> && ...);
    requires Detail::error_formatted_string_buffer_encoded_size<Ts...>() <= sizeof(FormatBuffer) - sizeof(u8);
};

class [[nodiscard]] Error {
    struct ErrnoCode {
        int code;

        bool operator==(ErrnoCode const&) const = default;
    };

#ifndef KERNEL
    struct Syscall {
        int code;
        StringView syscall_name;

        bool operator==(Syscall const&) const = default;
    };

    struct FormattedString {
        enum class Type : u8 {
            Nothing,
            StringView,
            StringImpl,
            ShortStringImpl,
            U8,
            U16,
            U32,
            U64,
            I8,
            I16,
            I32,
            I64,
        };

        bool operator==(FormattedString const&) const { return false; }

        StringView format_string;
        Array<u8, 64 - sizeof(StringView)> buffer; // See comment on `AK::FitsInErrorFormattedStringBuffer' for encoding.
    };

    struct String {
        u8 length;
        char buffer[64 - 1];

        bool operator==(String const& x) const { return StringView(buffer, length) == StringView(x.buffer, x.length); }
    };

    template<typename... Ts>
    Error(CheckedFormatString<Ts...> format_string, Ts... values)
        : m_data(FormattedString { format_string.view(), pack(values...) })
    {
    }

    enum class DynamicStringTag { Tag };
    Error(DynamicStringTag, StringView v)
        : m_data(String { .length = 0, .buffer = {} })
    {
        auto& string = m_data.get<String>();
        VERIFY(v.length() <= array_size(string.buffer));
        string.length = v.length();
        auto ok = v.copy_characters_to_buffer(&string.buffer[0], array_size(string.buffer));
        VERIFY(ok);
    }

    template<typename... Ts>
    static decltype(auto) pack(Ts&&... values)
    {
        decltype(FormattedString::buffer) buffer {};
        size_t offset = 0;
        ([&]<typename T>(T const& value) {
            FormattedString::Type tag;
            if constexpr (IsSame<T, i8>)
                tag = FormattedString::Type::I8;
            else if constexpr (IsSame<T, u8>)
                tag = FormattedString::Type::U8;
            else if constexpr (IsSame<T, i16>)
                tag = FormattedString::Type::I16;
            else if constexpr (IsSame<T, u16>)
                tag = FormattedString::Type::U16;
            else if constexpr (IsSame<T, i32>)
                tag = FormattedString::Type::I32;
            else if constexpr (IsSame<T, u32>)
                tag = FormattedString::Type::U32;
            else if constexpr (IsSame<T, i64>)
                tag = FormattedString::Type::I64;
            else if constexpr (IsSame<T, u64>)
                tag = FormattedString::Type::U64;
            else if constexpr (IsSame<T, StringView>)
                tag = FormattedString::Type::StringView;
            else if constexpr (IsSame<T, AK::String>)
                tag = FormattedString::Type::StringImpl;
            else
                static_assert(DependentFalse<T>, "Error::formatted() can only be passed (static) StringViews, AK::String, and integers");

            size_t size = tag == FormattedString::Type::StringImpl ? sizeof(FlatPtr) : sizeof(T);
            if (offset + size + 2 >= buffer.size())
                VERIFY_NOT_REACHED();

            if (tag == FormattedString::Type::StringImpl) {
                offset += pack_string(buffer.data(), offset, value);
            } else {
                buffer[offset] = to_underlying(tag);
                memcpy(&buffer[offset + 1], bit_cast<u8 const*>(&value), size);
                offset += size + 1;
            }
        }(values),
            ...);

        buffer[offset] = to_underlying(FormattedString::Type::Nothing);

        return buffer;
    }

    static size_t pack_string(u8* buffer, size_t offset, AK::String const& string);
#endif

public:
    ALWAYS_INLINE Error(Error&& other)
        : m_data(move(other.m_data))
    {
        other.m_data = ErrnoCode { 0 };
    }

    ALWAYS_INLINE Error& operator=(Error&& other)
    {
        m_data = move(other.m_data);
        other.m_data = ErrnoCode { 0 };
        return *this;
    }

    static Error from_errno(int code)
    {
        VERIFY(code != 0);
        return Error(code);
    }

    static Error copy(Error const& error)
    {
        return Error(error);
    }

    // NOTE: For calling this method from within kernel code, we will simply print
    // the error message and return the errno code.
    // For calling this method from userspace programs, we will simply return from
    // the Error::from_string_view method!
    static Error from_string_view_or_print_error_and_return_errno(StringView string_literal, int code);

#ifndef KERNEL
    static Error from_syscall(StringView syscall_name, int rc)
    {
        return Error(syscall_name, rc);
    }
    static Error from_string_view(StringView string_literal) { return Error(string_literal); }

    template<OneOf<DeprecatedString, DeprecatedFlyString, String, FlyString> T>
    static Error from_string_view(T)
    {
        // `Error::from_string_view(DeprecatedString::formatted(...))` is a somewhat common mistake, which leads to a UAF situation.
        // If your string outlives this error and _isn't_ a temporary being passed to this function, explicitly call .view() on it to resolve to the StringView overload.
        static_assert(DependentFalse<T>, "Error::from_string_view(String) is almost always a use-after-free");
        VERIFY_NOT_REACHED();
    }

    [[nodiscard]] static Error from_kinda_short_string(StringView string) { return Error(DynamicStringTag::Tag, string); }
    template<typename... Ts>
    requires(FitsInErrorFormattedStringBuffer<decltype(FormattedString::buffer), Ts...>)
    [[nodiscard]] static Error formatted(CheckedFormatString<Ts...> format_string, Ts... args)
    {
        return Error(format_string, args...);
    }

    // NOTE: Prefer `from_string_literal` when directly typing out an error message:
    //
    //     return Error::from_string_literal("Class: Some failure");
    //
    // If you need to return a static string based on a dynamic condition (like
    // picking an error from an array), then prefer `from_string_view` instead.
    template<size_t N>
    ALWAYS_INLINE static Error from_string_literal(char const (&string_literal)[N])
    {
        return from_string_view(StringView { string_literal, N - 1 });
    }

    // Note: Don't call this from C++; it's here for Jakt interop (as the name suggests).
    template<SameAs<StringView> T>
    ALWAYS_INLINE static Error __jakt_from_string_literal(T string)
    {
        return from_string_view(string);
    }

    bool is_syscall() const { return m_data.has<Syscall>(); }

    StringView string_literal() const
    {
        return m_data.visit(
            [](Syscall const& x) {
                return x.syscall_name;
            },
            [](FormattedString const& x) {
                if (static_cast<FormattedString::Type>(x.buffer[0]) == FormattedString::Type::Nothing)
                    return x.format_string;
                return StringView {};
            },
            [](String const& x) {
                return StringView { x.buffer, x.length };
            },
            [](auto&) { return StringView {}; });
    }
#endif

    int code() const
    {
        return m_data.visit(
            [](ErrnoCode const& x) { return x.code; },
#ifndef KERNEL
            [](Syscall const& x) { return x.code; },
#endif
            [](auto&) -> int { return 0; });
    }

    bool is_errno() const { return code() != 0; }

    bool operator==(Error const& other) const
    {
        return m_data == other.m_data;
    }

#ifndef KERNEL
    ~Error();
#endif

#ifndef KERNEL
    template<typename R = ErrorOr<AK::String>>
    R format_impl() const;
#endif

protected:
    Error(int code)
        : m_data(ErrnoCode(code))
    {
    }

private:
#ifndef KERNEL
    Error(StringView string_literal)
        : m_data(FormattedString { string_literal, { 0 } })
    {
    }

    Error(StringView syscall_name, int rc)
        : m_data(Syscall { -rc, syscall_name })
    {
    }
#endif

    Error(Error const&) = default;
    Error& operator=(Error const&) = default;

#ifdef KERNEL
    Variant<ErrnoCode> m_data;
#else
    Variant<Syscall, FormattedString, String, ErrnoCode> m_data;
#endif
};

template<typename T, typename E>
class [[nodiscard]] ErrorOr {
    template<typename U, typename F>
    friend class ErrorOr;

public:
    using ResultType = T;
    using ErrorType = E;

    ErrorOr()
    requires(IsSame<T, Empty>)
        : m_value_or_error(Empty {})
    {
    }

    ALWAYS_INLINE ErrorOr(ErrorOr&&) = default;
    ALWAYS_INLINE ErrorOr& operator=(ErrorOr&&) = default;

    ErrorOr(ErrorOr const&) = delete;
    ErrorOr& operator=(ErrorOr const&) = delete;

    template<typename U>
    ALWAYS_INLINE ErrorOr(ErrorOr<U, ErrorType>&& value)
    requires(IsConvertible<U, T>)
        : m_value_or_error(value.m_value_or_error.visit([](U& v) { return Variant<T, ErrorType>(move(v)); }, [](ErrorType& error) { return Variant<T, ErrorType>(move(error)); }))
    {
    }

    template<typename U>
    ALWAYS_INLINE ErrorOr(U&& value)
    requires(
        requires { T(declval<U>()); } || requires { ErrorType(declval<RemoveCVReference<U>>()); })
        : m_value_or_error(forward<U>(value))
    {
    }

#ifdef AK_OS_SERENITY
    ErrorOr(ErrnoCode code)
        : m_value_or_error(Error::from_errno(code))
    {
    }
#endif

    T& value()
    {
        return m_value_or_error.template get<T>();
    }
    T const& value() const { return m_value_or_error.template get<T>(); }

    ErrorType& error() { return m_value_or_error.template get<ErrorType>(); }
    ErrorType const& error() const { return m_value_or_error.template get<ErrorType>(); }

    bool is_error() const { return m_value_or_error.template has<ErrorType>(); }

    T release_value() { return move(value()); }
    ErrorType release_error() { return move(error()); }

    T release_value_but_fixme_should_propagate_errors()
    {
        VERIFY(!is_error());
        return release_value();
    }

private:
    Variant<T, ErrorType> m_value_or_error;
};

template<typename ErrorType>
class [[nodiscard]] ErrorOr<void, ErrorType> : public ErrorOr<Empty, ErrorType> {
public:
    using ResultType = void;
    using ErrorOr<Empty, ErrorType>::ErrorOr;
};

}

#if USING_AK_GLOBALLY
using AK::Error;
using AK::ErrorOr;
#endif
