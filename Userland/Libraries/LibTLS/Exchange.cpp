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

#include <AK/Debug.h>
#include <LibCrypto/ASN1/DER.h>
#include <LibCrypto/KeyExchange/DH.h>
#include <LibCrypto/PK/Code/EMSA_PSS.h>
#include <LibTLS/TLSv12.h>

namespace TLS {

bool TLSv12::expand_key()
{
    u8 key[192]; // soooooooo many constants
    auto key_buffer = Bytes { key, sizeof(key) };

    auto is_aead = this->is_aead();

    if (m_context.master_key.size() == 0) {
        dbgln("expand_key() with empty master key");
        return false;
    }

    auto key_size = key_length();
    auto mac_size = mac_length();
    auto iv_size = iv_length();

    pseudorandom_function(
        key_buffer,
        m_context.master_key,
        (const u8*)"key expansion", 13,
        ReadonlyBytes { m_context.remote_random, sizeof(m_context.remote_random) },
        ReadonlyBytes { m_context.local_random, sizeof(m_context.local_random) });

    size_t offset = 0;
    if (is_aead) {
        iv_size = 4; // Explicit IV size.
    } else {
        memcpy(m_context.crypto.local_mac, key + offset, mac_size);
        offset += mac_size;
        memcpy(m_context.crypto.remote_mac, key + offset, mac_size);
        offset += mac_size;
    }

    auto client_key = key + offset;
    offset += key_size;
    auto server_key = key + offset;
    offset += key_size;
    auto client_iv = key + offset;
    offset += iv_size;
    auto server_iv = key + offset;
    offset += iv_size;

#if TLS_DEBUG
    dbgln("client key");
    print_buffer(client_key, key_size);
    dbgln("server key");
    print_buffer(server_key, key_size);
    dbgln("client iv");
    print_buffer(client_iv, iv_size);
    dbgln("server iv");
    print_buffer(server_iv, iv_size);
    if (!is_aead) {
        dbgln("client mac key");
        print_buffer(m_context.crypto.local_mac, mac_size);
        dbgln("server mac key");
        print_buffer(m_context.crypto.remote_mac, mac_size);
    }
#endif

    if (is_aead) {
        memcpy(m_context.crypto.local_aead_iv, client_iv, iv_size);
        memcpy(m_context.crypto.remote_aead_iv, server_iv, iv_size);

        m_aes_local.gcm = make<Crypto::Cipher::AESCipher::GCMMode>(ReadonlyBytes { client_key, key_size }, key_size * 8, Crypto::Cipher::Intent::Encryption, Crypto::Cipher::PaddingMode::RFC5246);
        m_aes_remote.gcm = make<Crypto::Cipher::AESCipher::GCMMode>(ReadonlyBytes { server_key, key_size }, key_size * 8, Crypto::Cipher::Intent::Decryption, Crypto::Cipher::PaddingMode::RFC5246);
    } else {
        memcpy(m_context.crypto.local_iv, client_iv, iv_size);
        memcpy(m_context.crypto.remote_iv, server_iv, iv_size);

        m_aes_local.cbc = make<Crypto::Cipher::AESCipher::CBCMode>(ReadonlyBytes { client_key, key_size }, key_size * 8, Crypto::Cipher::Intent::Encryption, Crypto::Cipher::PaddingMode::RFC5246);
        m_aes_remote.cbc = make<Crypto::Cipher::AESCipher::CBCMode>(ReadonlyBytes { server_key, key_size }, key_size * 8, Crypto::Cipher::Intent::Decryption, Crypto::Cipher::PaddingMode::RFC5246);
    }

    m_context.crypto.created = 1;

    return true;
}

void TLSv12::pseudorandom_function(Bytes output, ReadonlyBytes secret, const u8* label, size_t label_length, ReadonlyBytes seed, ReadonlyBytes seed_b)
{
    if (!secret.size()) {
        dbgln("null secret");
        VERIFY_NOT_REACHED();
    }

    // RFC 5246: "In this section, we define one PRF, based on HMAC.  This PRF with the
    //            SHA-256 hash function is used for all cipher suites defined in this
    //            document and in TLS documents published prior to this document when
    //            TLS 1.2 is negotiated."
    // Apparently this PRF _always_ uses SHA256
    Crypto::Authentication::HMAC<Crypto::Hash::SHA256> hmac(secret);

    auto l_seed_size = label_length + seed.size() + seed_b.size();
    u8 l_seed[l_seed_size];
    auto label_seed_buffer = Bytes { l_seed, l_seed_size };
    label_seed_buffer.overwrite(0, label, label_length);
    label_seed_buffer.overwrite(label_length, seed.data(), seed.size());
    if (seed_b.size() > 0)
        label_seed_buffer.overwrite(label_length + seed.size(), seed_b.data(), seed_b.size());

    auto digest_size = hmac.digest_size();

    u8 digest[digest_size];

    auto digest_0 = Bytes { digest, digest_size };

    digest_0.overwrite(0, hmac.process(label_seed_buffer).immutable_data(), digest_size);

    size_t index = 0;
    while (index < output.size()) {
        hmac.update(digest_0);
        hmac.update(label_seed_buffer);
        auto digest_1 = hmac.digest();

        auto copy_size = min(digest_size, output.size() - index);

        output.overwrite(index, digest_1.immutable_data(), copy_size);
        index += copy_size;

        digest_0.overwrite(0, hmac.process(digest_0).immutable_data(), digest_size);
    }
}

bool TLSv12::compute_master_secret(size_t length)
{
    if (m_context.premaster_key.size() == 0 || length < 48) {
        dbgln("there's no way I can make a master secret like this");
        dbgln("I'd like to talk to your manager about this length of {}", length);
        return false;
    }

    m_context.master_key.clear();
    m_context.master_key.grow(length);

    pseudorandom_function(
        m_context.master_key,
        m_context.premaster_key,
        (const u8*)"master secret", 13,
        ReadonlyBytes { m_context.local_random, sizeof(m_context.local_random) },
        ReadonlyBytes { m_context.remote_random, sizeof(m_context.remote_random) });

    m_context.premaster_key.clear();
#if TLS_DEBUG
    dbgln("master key:");
    print_buffer(m_context.master_key);
#endif
    expand_key();
    return true;
}

ByteBuffer TLSv12::build_certificate()
{
    PacketBuilder builder { MessageType::Handshake, m_context.options.version };

    Vector<const Certificate*> certificates;
    Vector<Certificate>* local_certificates = nullptr;

    if (m_context.is_server) {
        dbgln("Unsupported: Server mode");
        VERIFY_NOT_REACHED();
    } else {
        local_certificates = &m_context.client_certificates;
    }

    constexpr size_t der_length_delta = 3;
    constexpr size_t certificate_vector_header_size = 3;

    size_t total_certificate_size = 0;

    for (size_t i = 0; i < local_certificates->size(); ++i) {
        auto& certificate = local_certificates->at(i);
        if (!certificate.der.is_empty()) {
            total_certificate_size += certificate.der.size() + der_length_delta;

            // FIXME: Check for and respond with only the requested certificate types.
            if (true) {
                certificates.append(&certificate);
            }
        }
    }

    builder.append((u8)HandshakeType::CertificateMessage);

    if (!total_certificate_size) {
#if TLS_DEBUG
        dbgln("No certificates, sending empty certificate message");
#endif
        builder.append_u24(certificate_vector_header_size);
        builder.append_u24(total_certificate_size);
    } else {
        builder.append_u24(total_certificate_size + certificate_vector_header_size); // 3 bytes for header
        builder.append_u24(total_certificate_size);

        for (auto& certificate : certificates) {
            if (!certificate->der.is_empty()) {
                builder.append_u24(certificate->der.size());
                builder.append(certificate->der.bytes());
            }
        }
    }
    auto packet = builder.build();
    update_packet(packet);
    return packet;
}

ByteBuffer TLSv12::build_change_cipher_spec()
{
    PacketBuilder builder { MessageType::ChangeCipher, m_context.options.version, 64 };
    builder.append((u8)1);
    auto packet = builder.build();
    update_packet(packet);
    m_context.local_sequence_number = 0;
    return packet;
}

ByteBuffer TLSv12::build_server_key_exchange()
{
    dbgln("FIXME: build_server_key_exchange");
    return {};
}

ByteBuffer TLSv12::build_client_key_exchange()
{
    PacketBuilder builder { MessageType::Handshake, m_context.options.version };
    builder.append((u8)HandshakeType::ClientKeyExchange);

    switch (ephemeral_state()) {
    case EphemeralState::NotEphemeral:
        build_random(builder);
        break;
    case EphemeralState::EphemeralDH:
        dbgln("Building a client kex message for DHE!");
        build_dhe_public(builder);
        // Drop the ephemeral key, it's not useful anymore.
        m_context.dhe_key.clear();
        break;
    case EphemeralState::EphemeralECDH:
        TODO();
    }

    m_context.connection_status = ConnectionStatus::KeyExchange;

    auto packet = builder.build();

    update_packet(packet);

    return packet;
}

ssize_t TLSv12::handle_server_key_exchange(ReadonlyBytes buffer)
{
    size_t res = 0;

    if (buffer.size() < 3)
        return (i8)Error::NeedMoreData;

    size_t size = buffer[0] * 0x10000 + buffer[1] * 0x100 + buffer[2];
    res += 3;

    if (buffer.size() - res < size)
        return (i8)Error::NeedMoreData;

    if (size == 0)
        return res;

    auto ephemeral = ephemeral_state();

    VERIFY(ephemeral != EphemeralState::EphemeralECDH);

    auto has_dh_params = ephemeral == EphemeralState::EphemeralDH;

    auto read_dh_param = [](ReadonlyBytes in_buffer, ReadonlyBytes& out) -> ssize_t {
        size_t res = 0;
        if (in_buffer.size() < 2)
            return (i8)Error::NeedMoreData;
        u16 size = ntohs(*(const u16*)in_buffer.offset_pointer(0));
        res += 2;
        if (in_buffer.size() - res < size)
            return (i8)Error::NeedMoreData;

        out = in_buffer.slice(res, size);
        res += size;
        return res;
    };

    ReadonlyBytes dh_p, dh_g, dh_y;
    if (has_dh_params) {
        auto dh_res = read_dh_param(buffer.slice(res), dh_p);
        if (dh_res <= 0)
            return (i8)Error::BrokenPacket;
        res += dh_res;

        dh_res = read_dh_param(buffer.slice(res), dh_g);
        if (dh_res <= 0)
            return (i8)Error::BrokenPacket;
        res += dh_res;

        dh_res = read_dh_param(buffer.slice(res), dh_y);
        if (dh_res <= 0)
            return (i8)Error::BrokenPacket;
        res += dh_res;

        if constexpr (TLS_DEBUG || true) {
            dbgln("DH params (p, g, y):");
            print_buffer(dh_p);
            print_buffer(dh_g);
            print_buffer(dh_y);
        }
    }

    u8 hash_algorithm, sign_algorithm;
    ReadonlyBytes signature;
    {
        // Read the signature
        if (buffer.size() < 4 + res)
            return (i8)Error::BrokenPacket;

        hash_algorithm = buffer[res++];
        sign_algorithm = buffer[res++];
        u16 size = NetworkOrdered<u16>(*(const u16*)(buffer.offset_pointer(res)));
        res += 2;
        if (buffer.size() - res < size)
            return (i8)Error::BrokenPacket;

        signature = buffer.slice(res, size);
        res += size;
    }

    // FIXME: Actually check the signature.
    (void)hash_algorithm;
    (void)sign_algorithm;
    (void)signature;

    if constexpr (TLS_DEBUG) {
        if (buffer.size() > res) {
            dbgln("Extra bytes at the end of ServerKeyExchange message ({} bytes)", buffer.size() - res);
            print_buffer(buffer.slice(res));
        }
    }

    if (ephemeral == EphemeralState::EphemeralDH) {
        auto ephemeral_dh_key = [&](auto&& p, auto&& g) -> Crypto::KeyExchange::DHKey {
#if 0
            auto key_size = max(dh_p.size(), dh_g.size());
            auto x_buffer = ByteBuffer::create_uninitialized(key_size);
            fill_with_random(x_buffer.data(), x_buffer.size());
            auto x = Crypto::UnsignedBigInteger::import_data(x_buffer);
#else
            Crypto::UnsignedBigInteger x { 42 }; // FIXME: This is too random, make sure it's less random by actually running the code above.
#endif
            dbgln("Gonna calculate e{}^e{} mod e{}, brace yourself...", g.trimmed_length(), x.trimmed_length(), p.trimmed_length());
            return {
                x,
                Crypto::NumberTheory::ModularPower(g, x, p),
                p,
                g,
            };
        };

        auto make_dh_secret = [&] {
            auto random_key = ephemeral_dh_key(
                Crypto::UnsignedBigInteger::import_data(dh_p),
                Crypto::UnsignedBigInteger::import_data(dh_g));

            Crypto::KeyExchange::DHKey server_key { {}, Crypto::UnsignedBigInteger::import_data(dh_y), {}, {}, {} };

            m_context.dhe_key = move(random_key);

            auto buffer = ByteBuffer::create_uninitialized(dh_y.size());
            auto bytes = buffer.bytes();
            Crypto::KeyExchange::DH { m_context.dhe_key.value(), server_key }.generate_shared_secret(bytes);

            buffer.trim(bytes.size());
            return buffer;
        };

        m_context.premaster_key = make_dh_secret();

        if constexpr (TLS_DEBUG || true) {
            dbgln("Ephemeral exchange complete, premaster key is:");
            print_buffer(m_context.premaster_key);
        }
    }
    return res;
}

ssize_t TLSv12::handle_verify(ReadonlyBytes)
{
    dbgln("FIXME: parse_verify");
    return 0;
}
}
