/*
 * Copyright (c) 2020, the SerenityOS developers.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <AK/RefCounted.h>
#include <AK/RefPtr.h>
#include <AK/Types.h>

namespace AK {

template<typename T, typename U>
class AATree;

namespace {

template<typename T>
struct LessThan {
    constexpr bool operator()(const T& a, const T& b)
    {
        return a < b;
    }
};

template<typename DataT, typename Comparator = LessThan<DataT>>
class AATreeNode : public RefCounted<AATreeNode<DataT, Comparator>> {
    friend AATree<DataT, Comparator>;

public:
    AATreeNode(DataT data)
        : m_data(data)
        , m_level(1)
    {
    }

    ~AATreeNode() { }

    RefPtr<AATreeNode> next() const
    {
        if (m_right) {
            auto* value = m_right.ptr();
            while (value->m_left)
                value = value->m_left.ptr();

            return *value;
        }

        auto* value = this;
        while (value->m_parent && value->m_parent->m_right == value)
            value = value->m_parent.ptr();

        return value->m_parent;
    }

    RefPtr<AATreeNode> previous() const
    {
        if (m_left) {
            auto* value = m_left.ptr();
            while (value->m_right)
                value = value->m_right.ptr();

            return *value;
        }

        auto* value = this;
        while (value->m_parent && value->m_parent->m_left == value)
            value = value->m_parent.ptr();

        return value->m_parent;
    }

    const RefPtr<AATreeNode>& left() const { return m_left; }
    const RefPtr<AATreeNode>& right() const { return m_right; }
    const RefPtr<AATreeNode>& parent() const { return m_parent; }
    const DataT& data() const { return m_data; }
    size_t level() const { return m_level; }

private:
    RefPtr<AATreeNode> m_left;
    RefPtr<AATreeNode> m_right;
    AATreeNode* m_parent;
    DataT m_data;
    size_t m_level { 1 };
};

}

template<typename DataT, typename Comparator = LessThan<DataT>>
class AATree {
public:
    using NodeType = AATreeNode<DataT, Comparator>;

    void insert(const DataT& data)
    {
        m_root = insert(adopt(*new AATreeNode<DataT, Comparator>(data)), m_root);
        m_root->m_parent = nullptr;
    }

    void insert(DataT&& data)
    {
        m_root = insert(adopt(*new AATreeNode<DataT, Comparator>(move(data))), m_root);
        m_root->m_parent = nullptr;
    }

    // FIXME: remove()

private:
    constexpr static Comparator comparator {};

    RefPtr<NodeType> skew(RefPtr<NodeType> root)
    {
        if (!root)
            return root;

        if (root->left()->level() == root->level()) {
            // Rotate right.
            auto temp = root;
            root = root->left();
            temp->m_left = root->right();
            temp->m_left->m_parent = temp;
            root->m_right = temp;
            root->m_right->m_parent = root;
        }

        root->m_right = skew(root->m_right);
        root->m_right->m_parent = root;

        return root;
    }

    RefPtr<NodeType> split(RefPtr<NodeType> root)
    {
        if (!root)
            return root;

        if (root->right() && root->right()->right() && root->right()->right()->level() == root->level()) {
            auto temp = root;
            root = root->right();
            temp->m_right = root->left();
            temp->m_right->m_parent = temp;
            root->m_left = temp;
            root->m_left->m_parent = root;

            root->m_level++;

            root->m_right = split(root->m_right);
            root->m_right->m_parent = root;
        }

        return root;
    }

    NonnullRefPtr<NodeType> insert(NonnullRefPtr<NodeType> node, RefPtr<NodeType> root)
    {
        if (root) {
            if (comparator(node, root)) {
                root->m_left = insert(node, root->m_left);
                root->m_left->m_parent = root;
            } else {
                root->m_right = insert(node, root->m_right);
                root->m_right->m_parent = root;
            }
            root = split(skew(root));
        } else {
            root = node;
            node->m_parent = nullptr;
            ++m_count;
        }

        return root;
    }

    RefPtr<NodeType> m_root;
    size_t m_count { 0 };
};

}
