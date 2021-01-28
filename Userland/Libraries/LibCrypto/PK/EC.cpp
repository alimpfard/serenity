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

#include <LibCrypto/NumberTheory/ModularFunctions.h>
#include <LibCrypto/PK/EC.h>

namespace {

auto invert(const Crypto::PK::Integer& z, const Crypto::PK::Integer& modulo) -> Optional<Crypto::PK::Integer>
{
    auto egcd = Crypto::NumberTheory::EGCD(z.divided_by(modulo).remainder, modulo);
    if (egcd.result != 1) {
        dbgln("Invalid inversion({}, {}) op, result does not exist", z, modulo);
        return {};
    }
    return egcd.bezout_x.divided_by(modulo).remainder;
}

struct JacobianPoint {

    static JacobianPoint from_affine(const Crypto::PK::Curve& curve, const Crypto::PK::Point& point) { return { point.x(), point.y(), 1, curve }; }
    Crypto::PK::Point to_affine() const
    {
        auto z_inverse = invert(z, curve.p).value_or(0);
        auto z_inverse2 = z_inverse.multiplied_by(z_inverse);
        auto affine_x = x.multiplied_by(z_inverse2).divided_by(curve.p).remainder;
        auto affine_y = y.multiplied_by(z_inverse2).multiplied_by(z_inverse).divided_by(curve.p).remainder;
        return { move(affine_x), move(affine_y) };
    }

    auto operator==(const JacobianPoint& other) const
    {
        if (&curve != &other.curve)
            return false;

        auto az2 = Crypto::NumberTheory::ModularPower(z, 2, curve.p);
        auto az3 = Crypto::NumberTheory::ModularPower(z, 3, curve.p);
        auto bz2 = Crypto::NumberTheory::ModularPower(other.z, 2, curve.p);
        auto bz3 = Crypto::NumberTheory::ModularPower(other.z, 3, curve.p);

        return bz2.multiplied_by(x).divided_by(curve.p).remainder == az2.multiplied_by(other.x).divided_by(curve.p).remainder
            && bz3.multiplied_by(y).divided_by(curve.p).remainder == az3.multiplied_by(other.y).divided_by(curve.p).remainder;
    }

    void negate()
    {
        y = decltype(y) { 0 }.minus(y);
    }

    void double_()
    {
        auto two = decltype(y) { 2 };
        auto three = decltype(y) { 3 };
        auto eight = decltype(y) { 8 };

        auto a = x.multiplied_by(x);
        auto b = y.multiplied_by(y);
        auto c = z.multiplied_by(z);
        auto xpb = x.plus(b);
        auto d = two.multiplied_by(xpb.multiplied_by(xpb).minus(a).minus(c));
        auto e = three.multiplied_by(a);
        auto f = e.multiplied_by(e);

        // Note: Order matters!
        z = two.multiplied_by(y).multiplied_by(z);
        x = f.minus(two.multiplied_by(d)).divided_by(curve.p).remainder;
        y = e.multiplied_by(d.minus(x).minus(eight.multiplied_by(c))).divided_by(curve.p).remainder;
    }

    void add(const JacobianPoint& other)
    {
    }

    Crypto::PK::Integer x;
    Crypto::PK::Integer y;
    Crypto::PK::Integer z;
    const Crypto::PK::Curve& curve;
};

}

namespace Crypto::PK {

Point Point::times_two() const
{
}

Point Point::negated() const
{
}

Point Point::add(const Point&) const
{
}

Point Point::subtract(const Point&) const
{
}

Point Point::multiply(const Integer&) const
{
}

}
