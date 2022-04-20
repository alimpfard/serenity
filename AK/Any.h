/*
 * Copyright (c) 2022, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/Variant.h>
#include <stdlib.h>

namespace AK::Detail {
template<typename T>
constexpr size_t align_of_workaround()
{
    // FIXME: `alignof(T)` is 4 on i686, but UBSAN wants it to be 8, so we'll work around this for now.
    if constexpr (IsSame<T, double>)
        return 8;

    return alignof(T);
}

template<size_t MinInlineSize, size_t MinInlineAlignment>
struct AnyStorage {
    constexpr static size_t PreferredAlignment = max(alignof(void*), MinInlineAlignment);
    constexpr static size_t PreferredSize = max(sizeof(void*), MinInlineSize);
    constexpr static size_t InvalidID = 0;

    AnyStorage()
        : type_id(InvalidID)
        , data()
        , destructor(nullptr)
        , copy_constructor(nullptr)
        , move_constructor(nullptr)
    {
    }

    AnyStorage(AnyStorage const&) = delete;
    AnyStorage(AnyStorage&& other)
    {
        set(Empty {});

        if (other.m_type_id == InvalidID)
            return;

        type_id = other.type_id;
        destructor = move(other.destructor);
        copy_constructor = move(other.copy_constructor);
        move_constructor = move(other.move_constructor);

        if (move_constructor)
            move_constructor(other, *this);
        else
            memcpy(data, other.data, PreferredSize);
    }

    AnyStorage copy() const
    {
        AnyStorage copy;
        copy.type_id = type_id;
        copy.destructor = destructor;
        copy.copy_constructor = copy_constructor;
        copy.move_constructor = move_constructor;

        if (copy_constructor)
            copy_constructor(*this, copy);
        else
            memcpy(copy.data, data, PreferredSize);

        return copy;
    }

    struct TypeContainer {
        static size_t next_id()
        {
            static Atomic<size_t> id = 1;
            return id.fetch_add(1);
        }

        template<typename T>
        static size_t id()
        {
            static size_t id = next_id();
            return id;
        }
    };

    template<typename T>
    T& as()
    {
        VERIFY(is<T>());

        if constexpr (PreferredAlignment >= align_of_workaround<T>() && PreferredSize >= sizeof(T))
            return *bit_cast<T*>(&data[0]);
        else
            return **bit_cast<T**>(&data[0]);
    }

    template<typename T>
    T const& as() const
    {
        return const_cast<AnyStorage*>(this)->as<T>();
    }

    template<typename T>
    bool is() const
    {
        return type_id == TypeContainer::template id<T>();
    }

    void set(Empty)
    {
        if (destructor)
            destructor(*this);

        type_id = InvalidID;
    }

    template<typename V>
    void set(V&& value)
    {
        using T = RemoveCVReference<V>;

        if (destructor)
            destructor(*this);

        type_id = TypeContainer::template id<T>();

        if constexpr (PreferredAlignment >= align_of_workaround<T>() && PreferredSize >= sizeof(T)) {
            new (bit_cast<T*>(&data[0])) T(forward<V>(value));

            if constexpr (IsTriviallyDestructible<T>) {
                destructor = nullptr;
            } else {
                destructor = [](AnyStorage& storage) {
                    storage.as<T>().~T();
                };
            }

            if constexpr (IsTriviallyCopyConstructible<T>) {
                copy_constructor = nullptr;
            } else {
                copy_constructor = [](AnyStorage const& from, AnyStorage& to) {
                    new (&to.as<T>()) T(from.as<T>());
                };
            }

            if constexpr (IsTriviallyMoveConstructible<T>) {
                move_constructor = nullptr;
            } else {
                move_constructor = [](AnyStorage& from, AnyStorage& to) {
                    new (&to.as<T>()) T(move(from.as<T>()));
                };
            }
        } else {
            *bit_cast<T**>(&data[0]) = ::new T(forward<V>(value));

            destructor = [](AnyStorage& storage) {
                delete *bit_cast<T**>(&storage.data[0]);
            };

            if constexpr (IsTriviallyCopyConstructible<T>) {
                copy_constructor = nullptr;
            } else {
                copy_constructor = [](AnyStorage const& from, AnyStorage& to) {
                    *bit_cast<T**>(&to.data[0]) = ::new T(from.as<T>());
                };
            }

            if constexpr (IsTriviallyMoveConstructible<T>) {
                move_constructor = nullptr;
            } else {
                move_constructor = [](AnyStorage& from, AnyStorage& to) {
                    *bit_cast<T**>(&to.data[0]) = ::new T(move(from.as<T>()));
                };
            }
        }
    }

    ~AnyStorage()
    {
        if (destructor)
            destructor(*this);
    }

    size_t type_id { InvalidID };
    alignas(PreferredAlignment) u8 data[PreferredSize];

    Function<void(AnyStorage&)> destructor;
    Function<void(AnyStorage const& from, AnyStorage& to)> copy_constructor;
    Function<void(AnyStorage& from, AnyStorage& to)> move_constructor;
};
}

namespace AK {
template<typename... Inlines>
class AnyWithInlineStorage {
public:
    AnyWithInlineStorage(Empty = {})
    {
        m_storage.set(Empty {});
    }

    template<typename T>
    AnyWithInlineStorage(T&& value)
        : m_storage({})
    {
        m_storage.set(forward<T>(value));
    }

    AnyWithInlineStorage(AnyWithInlineStorage&& other)
        : m_storage(move(other.m_storage))
    {
    }

    AnyWithInlineStorage(AnyWithInlineStorage const& other)
        : m_storage(other.m_storage.copy())
    {
    }

    AnyWithInlineStorage& operator=(AnyWithInlineStorage&& other)
    {
        if (this == &other)
            return *this;

        m_storage = move(other.m_storage);
        return *this;
    }

    AnyWithInlineStorage& operator=(AnyWithInlineStorage const& other)
    {
        if (this == &other)
            return *this;

        m_storage = other.m_storage.copy();
        return *this;
    }

    template<typename T>
    AnyWithInlineStorage& operator=(T&& value)
    {
        m_storage.set(forward<T>(value));
        return *this;
    }

    template<typename T>
    T& get()
    {
        return m_storage.template as<T>();
    }

    template<typename T>
    T const& get() const
    {
        return const_cast<RemoveCV<decltype(m_storage)>>(m_storage).template as<T>();
    }

    template<typename T>
    bool is() const
    {
        return m_storage.template is<T>();
    }

private:
    template<size_t... values>
    static constexpr auto max_of()
    {
        Array vs { values... };
        return vs.max();
    }

    using Storage = Detail::AnyStorage<max_of<0u, sizeof(Inlines)...>(), max_of<1u, Detail::align_of_workaround<Inlines>()...>()>;
    Storage m_storage;
};

using Any = AnyWithInlineStorage<>;
}

using AK::Any;
using AK::AnyWithInlineStorage;
