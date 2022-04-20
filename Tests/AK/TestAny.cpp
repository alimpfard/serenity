/*
 * Copyright (c) 2022, Ali Mohammad Pur <mpfard@serenity.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestSuite.h>

#include <AK/Any.h>

TEST_CASE(basic)
{
    Any x { 123 };
    EXPECT_EQ(x.get<int>(), 123);
    x = float(1.2);
    EXPECT_EQ(x.get<float>(), 1.2f);

    struct Foo {
        int x;
    };
    Foo foo { 42 };

    x = foo;
    EXPECT_EQ(x.get<Foo>().x, 42);
}

TEST_CASE(destructor)
{
    struct DestructionChecker {
        explicit DestructionChecker(bool& was_destroyed)
            : m_was_destroyed(was_destroyed)
        {
        }

        ~DestructionChecker()
        {
            m_was_destroyed = true;
        }
        bool& m_was_destroyed;
    };

    bool was_destroyed = false;
    {
        Any x { DestructionChecker(was_destroyed) };
    }
    EXPECT(was_destroyed);

    was_destroyed = false;
    {
        Any x { DestructionChecker(was_destroyed) };
        x = 123;
    }
    EXPECT(was_destroyed);
}

TEST_CASE(is)
{
    Any x { 123 };
    EXPECT(x.is<int>());
    EXPECT(!x.is<float>());
    EXPECT(!x.is<double>());
    EXPECT(!x.is<bool>());
    EXPECT(!x.is<void*>());
    EXPECT(!x.is<String>());

    x = float(1.2);
    EXPECT(!x.is<int>());
    EXPECT(x.is<float>());

    x = double(1.2);
    EXPECT(!x.is<int>());
    EXPECT(!x.is<float>());
    EXPECT(x.is<double>());

    x = true;
    EXPECT(!x.is<int>());
    EXPECT(!x.is<float>());
    EXPECT(!x.is<double>());
    EXPECT(x.is<bool>());
}

TEST_CASE(constructor)
{
    struct ConstructorChecker {
        explicit ConstructorChecker(bool& was_move_constructed, bool& was_copy_constructed)
            : m_was_move_constructed(was_move_constructed)
            , m_was_copy_constructed(was_copy_constructed)
        {
        }

        ConstructorChecker(ConstructorChecker&& other)
            : m_was_move_constructed(other.m_was_move_constructed)
            , m_was_copy_constructed(other.m_was_copy_constructed)
        {
            other.m_was_move_constructed = true;
            other.m_was_copy_constructed = false;
        }

        ConstructorChecker(ConstructorChecker const& other)
            : m_was_move_constructed(other.m_was_move_constructed)
            , m_was_copy_constructed(other.m_was_copy_constructed)
        {
            m_was_move_constructed = false;
            m_was_copy_constructed = true;
        }

        bool& m_was_move_constructed;
        bool& m_was_copy_constructed;
    };

    bool was_move_constructed = false;
    bool was_copy_constructed = false;

    {
        Any x { ConstructorChecker(was_move_constructed, was_copy_constructed) };
    }
    EXPECT(was_move_constructed);
    EXPECT(!was_copy_constructed);

    was_move_constructed = false;
    was_copy_constructed = false;
    {
        Any x { 123 };
        auto c = ConstructorChecker(was_move_constructed, was_copy_constructed);
        x = c;
    }
    EXPECT(!was_move_constructed);
    EXPECT(was_copy_constructed);
}
