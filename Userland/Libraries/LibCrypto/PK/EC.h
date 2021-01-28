/*
 * Copyright (c) 2021, the SerenityOS developers.
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

#include <LibCrypto/BigInt/UnsignedBigInteger.h>
#include <LibCrypto/PK/PK.h>

namespace Crypto::PK {

using Integer = UnsignedBigInteger;

struct Curve {
    AK_MAKE_NONCOPYABLE(Curve);
    AK_MAKE_NONMOVABLE(Curve);

public:
    Integer a;
    Integer b;
    Integer p;
    Integer n;
    Integer h;
    Integer g_x;
    Integer g_y;
    Integer beta;
};

class Point {
public:
    Point(Integer x, Integer y)
        : m_x(move(x))
        , m_y(move(y))
    {
    }

    Point(const Curve&, Integer private_key);

    ByteBuffer to_bytes() const;

    const Integer& x() const { return m_x; }
    const Integer& y() const { return m_y; }

    auto operator==(const Point& other) const { return m_x == other.m_x && m_y == other.m_y; }

    Point times_two() const;
    Point negated() const;
    Point add(const Point&) const;
    Point subtract(const Point&) const;
    Point multiply(const Integer&) const;

private:
    Integer m_x;
    Integer m_y;
};

using ECKey = Point;

class EC : public PKSystem<ECKey, ECKey> {
public:
    static Curve s_secp256k1;

private:
};

}
