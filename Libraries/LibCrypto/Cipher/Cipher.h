/*
 * Copyright (c) 2020, Ali Mohammad Pur <ali.mpfard@gmail.com>
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

#include <AK/ByteBuffer.h>
#include <AK/Optional.h>
#include <AK/RefPtr.h>
#include <AK/Types.h>

namespace Crypto {

enum class Intent {
    Encryption,
    Decryption,
};

template <typename B, typename T>
class Cipher;

struct CipherBlock {
public:
    static size_t block_size() { ASSERT_NOT_REACHED(); }

    virtual ByteBuffer get() const = 0;
    virtual const ByteBuffer& data() const = 0;

    virtual void overwrite(const ByteBuffer&) = 0;
    virtual void overwrite(const u8*, size_t) = 0;

    virtual void apply_initialization_vector(const u8* ivec) = 0;

    template <typename T>
    void put(size_t offset, T value)
    {
        ASSERT(offset + sizeof(T) <= data().size());
        auto* ptr = data().data() + offset;
        auto index { 0 };

        ASSERT(sizeof(T) <= 4);

        if constexpr (sizeof(T) > 3)
            ptr[index++] = (u8)(value >> 24);

        if constexpr (sizeof(T) > 2)
            ptr[index++] = (u8)(value >> 16);

        if constexpr (sizeof(T) > 1)
            ptr[index++] = (u8)(value >> 8);

        ptr[index] = (u8)value;
    }

private:
    virtual ByteBuffer& data() = 0;
};

struct CipherKey {
    virtual ByteBuffer data() const = 0;
    static bool is_valid_key_size(size_t) { return false; };

    virtual ~CipherKey() {}

protected:
    virtual void expand_encrypt_key(const StringView& user_key, size_t bits) = 0;
    virtual void expand_decrypt_key(const StringView& user_key, size_t bits) = 0;
    size_t bits;
};

template <typename KeyT = CipherKey, typename BlockT = CipherBlock>
class Cipher {
public:
    using KeyType = KeyT;
    using BlockType = BlockT;

    virtual const KeyType& key() const = 0;
    virtual KeyType& key() = 0;

    static size_t block_size() { return BlockType::block_size(); }

    virtual void encrypt_block(const BlockType& in, BlockType& out) = 0;
    virtual void decrypt_block(const BlockType& in, BlockType& out) = 0;
};
}
