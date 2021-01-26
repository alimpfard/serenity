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

#include <AK/Span.h>
#include <AK/Vector.h>
#include <LibCrypto/BigInt/UnsignedBigInteger.h>
#include <LibCrypto/NumberTheory/ModularFunctions.h>
#include <LibCrypto/PK/PK.h>

namespace Crypto {
namespace PK {

class DHKey final {
public:
    DHKey(size_t size, ReadonlyBytes p, ReadonlyBytes g);

    UnsignedBigInteger derive_shared_key(const DHKey& other);
    size_t max_size() const { return m_p.trimmed_length(); }

    // FIXME: Import/Export

private:
    int m_iana { 0 };
    UnsignedBigInteger m_x;
    UnsignedBigInteger m_y;
    UnsignedBigInteger m_p;
    UnsignedBigInteger m_g;
};

class DH final : public PKSystem<DHKey, DHKey> {
public:
    virtual void encrypt(ReadonlyBytes in, Bytes& out) override;
    virtual void decrypt(ReadonlyBytes in, Bytes& out) override;

    virtual void sign(ReadonlyBytes, Bytes&) override { ASSERT_NOT_REACHED(); }
    virtual void verify(ReadonlyBytes, Bytes&) override { ASSERT_NOT_REACHED(); }

    virtual String class_name() const override { return "DH"; };

    virtual size_t output_size() const override { return m_private_key.max_size(); };
};

}
}
