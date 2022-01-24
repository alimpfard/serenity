/*
 * Copyright (c) 2022, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Badge.h>
#include <AK/OwnPtr.h>
#include <AK/RefCounted.h>
#include <AK/Vector.h>
#include <LibJS/Forward.h>
#include <LibJS/SourceRange.h>

namespace JS {

template<typename T>
class NodePoolEntry;

class NodePool {
public:
    class Node {
        friend NodePool;
        Optional<size_t> m_id;

    public:
        virtual ~Node() = default;
    };

    template<typename T, typename... Args>
    NonnullNodePtr<T> create_ast_node(SourceRange range, Args&&... args)
    {
        return create_node<T>(range, forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    NonnullNodePtr<T> create_node(Args&&... args);

    static NodePool& the();

    template<typename T = Node>
    T& node(size_t index) { return *static_cast<T*>(m_pool[index].ptr()); }

    template<typename T = Node>
    T const& node(size_t index) const { return *static_cast<T const*>(m_pool[index].ptr()); }

    Optional<size_t> free_id()
    {
        if (m_free_ids.is_empty())
            return {};

        return m_free_ids.take_last();
    }

    template<typename T>
    void did_destroy_node(size_t id, Badge<NodePoolEntry<T>>)
    {
        if (m_deleting)
            return;

        m_free_ids.append(id);
        // Defer the deletion unless we have too many deferred deletes
        // then drop it down to some arbitrary number.
        size_t node_id_index = 0;
        while (m_deferred_deletions > maximum_deferred_deletions_allowed) {
            --m_deferred_deletions;
            m_pool[m_free_ids[node_id_index]] = nullptr;
            ++node_id_index;
        }
    }

    template<typename T>
    void swap_underlying(Node& node0, Node& node1, Badge<NodePoolEntry<T>>)
    {
        swap(m_pool[*node0.m_id], m_pool[*node1.m_id]);
    }

    ~NodePool()
    {
        m_deleting = true;
    }

private:
    constexpr static size_t maximum_deferred_deletions_allowed = 128;

    Vector<OwnPtr<Node>> m_pool;
    Vector<size_t> m_free_ids;
    size_t m_deferred_deletions { 0 };
    bool m_deleting { false };
};

template<typename T>
class NodePoolEntry : public RefCounted<NodePoolEntry<T>> {
    template<typename U>
    friend class NodePoolEntry;

public:
    NodePoolEntry(NodePool& pool, size_t id)
        : m_pool(pool)
        , m_id(id)
    {
    }

    ~NodePoolEntry()
    {
        m_pool.did_destroy_node<T>(m_id, {});
    }

    template<typename U>
    requires(IsSame<T, U> || IsBaseOf<U, T>) NodePoolEntry(NodePoolEntry<U> const& entry)
        : m_pool(entry.m_pool)
        , m_id(entry.m_id)
    {
    }

    T& operator*() { return m_pool.node<T>(m_id); }
    T const& operator*() const { return m_pool.node<T>(m_id); }
    T* operator->() { return &m_pool.node<T>(m_id); }
    T const* operator->() const { return m_pool.node<T>(m_id); }

    NodePool& pool() { return m_pool; }

    void reseat_node(NodePool::Node& node_to_replace)
    {
        m_pool.swap_underlying<T>(**this, node_to_replace, {});
    }

private:
    NodePool& m_pool;
    size_t m_id;
};

template<typename T>
class NonnullNodePtr : public NonnullRefPtr<NodePoolEntry<T>> {
public:
    using NonnullRefPtr<NodePoolEntry<T>>::NonnullRefPtr;

    explicit NonnullNodePtr(NonnullRefPtr<NodePoolEntry<T>>&& ptr)
        : NonnullRefPtr<NodePoolEntry<T>>(move(ptr))
    {
    }

    template<typename U>
    requires(IsBaseOf<T, U> && !IsSame<U, T>) NonnullNodePtr(NonnullNodePtr<U> const& ptr)
        : NonnullRefPtr<NodePoolEntry<T>>(*bit_cast<NodePoolEntry<T> const*>(ptr.NonnullRefPtr<NodePoolEntry<U>>::ptr()))
    {
    }

    template<typename U>
    requires(IsBaseOf<U, T> && !IsSame<U, T>) NonnullNodePtr(NonnullNodePtr<U>&& ptr)
        : NonnullRefPtr<NodePoolEntry<T>>(*bit_cast<NodePoolEntry<T>*>(ptr.NonnullRefPtr<NodePoolEntry<U>>::ptr()))
    {
    }

    NodePoolEntry<T>& node() { return *NonnullRefPtr<NodePoolEntry<T>>::ptr(); }
    NodePoolEntry<T> const& node() const { return *NonnullRefPtr<NodePoolEntry<T>>::ptr(); }

    T* ptr() { return &**NonnullRefPtr<NodePoolEntry<T>>::ptr(); }
    T const* ptr() const { return &**NonnullRefPtr<NodePoolEntry<T>>::ptr(); }

    T& operator*() { return *ptr(); }
    T const& operator*() const { return *ptr(); }
    T* operator->() { return ptr(); }
    T const* operator->() const { return ptr(); }
};

template<typename T>
class NodePtr : public RefPtr<NodePoolEntry<T>> {
public:
    using RefPtr<NodePoolEntry<T>>::RefPtr;

    template<typename U>
    requires(IsBaseOf<T, U> && !IsSame<U, T>) NodePtr(NodePtr<U> const& ptr)
        : RefPtr<NodePoolEntry<T>>(*bit_cast<NodePoolEntry<T> const*>(ptr.RefPtr<NodePoolEntry<U>>::ptr()))
    {
    }

    template<typename U>
    requires(IsBaseOf<U, T> && !IsSame<U, T>) NodePtr(NodePtr<U>&& ptr)
        : RefPtr<NodePoolEntry<T>>(*bit_cast<NodePoolEntry<T>*>(ptr.RefPtr<NodePoolEntry<U>>::ptr()))
    {
    }

    template<typename U>
    requires(IsBaseOf<T, U> && !IsSame<U, T>) NodePtr(NonnullNodePtr<U> const& ptr)
        : RefPtr<NodePoolEntry<T>>(*bit_cast<NodePoolEntry<T> const*>(ptr.NonnullRefPtr<NodePoolEntry<U>>::ptr()))
    {
    }

    template<typename U>
    requires(IsBaseOf<U, T> && !IsSame<U, T>) NodePtr(NonnullNodePtr<U>&& ptr)
        : RefPtr<NodePoolEntry<T>>(*bit_cast<NodePoolEntry<T>*>(ptr.NonnullRefPtr<NodePoolEntry<U>>::ptr()))
    {
    }

    NonnullNodePtr<T> verify_nonnull() const;
    NonnullNodePtr<T> verify_nonnull();
    NonnullNodePtr<T> release_nonnull()
    {
        auto ptr = verify_nonnull();
        *this = nullptr;
        return ptr;
    }

    NodePoolEntry<T>* node() { return RefPtr<NodePoolEntry<T>>::ptr(); }
    NodePoolEntry<T> const* node() const { return RefPtr<NodePoolEntry<T>>::ptr(); }

    T* ptr()
    {
        if (auto* node = this->node())
            return &**node;
        return nullptr;
    }

    T const* ptr() const
    {
        if (auto* node = this->node())
            return &**node;
        return nullptr;
    }

    T& operator*() { return *ptr(); }
    T const& operator*() const { return *ptr(); }
    T* operator->() { return ptr(); }
    T const* operator->() const { return ptr(); }
};

template<typename T, typename U>
NodePtr<T> static_ptr_cast(NodePtr<U> const& ptr)
{
    return NodePtr<T>(static_cast<const NodePoolEntry<T>*>(ptr.node()));
}

template<typename T, typename U>
NonnullNodePtr<T> static_ptr_cast(NonnullNodePtr<U> const& ptr)
{
    return NonnullNodePtr<T>(*bit_cast<NodePoolEntry<T> const*>(&ptr.node()));
}

template<typename T>
NonnullNodePtr<T> NodePtr<T>::verify_nonnull()
{
    VERIFY(ptr());
    return NonnullNodePtr<T>(*RefPtr<NodePoolEntry<T>>::ptr());
}

template<typename T>
NonnullNodePtr<T> NodePtr<T>::verify_nonnull() const
{
    VERIFY(ptr());
    return NonnullNodePtr<T>(*RefPtr<NodePoolEntry<T>>::ptr());
}

template<typename T, typename... Args>
NonnullNodePtr<T> NodePool::create_node(Args&&... args)
{
    auto node = make<T>(forward<Args>(args)...);

    size_t id;
    if (auto maybe_id = free_id(); maybe_id.has_value()) {
        id = maybe_id.value();
        m_pool[id] = move(node);
    } else {
        id = m_pool.size();
        m_pool.append(move(node));
    }

    m_pool[id]->m_id = id;

    return static_cast<NonnullNodePtr<T>>(make_ref_counted<NodePoolEntry<T>>(*this, id));
}

template<typename T, size_t inline_capacity = 0>
class NonnullNodePtrVector : public Vector<NonnullNodePtr<T>, inline_capacity> {
    using Vector<NonnullNodePtr<T>, inline_capacity>::Vector;
};

}
