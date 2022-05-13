/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-05-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/secrets.hh>
#include <maxbase/log.hh>
#include <maxbase/assert.hh>
#include <maxbase/alloc.hh>

#include <tuple>
#include <initializer_list>

#include <openssl/err.h>
#include <openssl/rand.h>

#define ENCRYPTING 1
#define DECRYPTING 0

namespace
{

using CipherFn = const EVP_CIPHER * (*)(void);

constexpr CipherFn get_cipher_fn(mxb::Cipher::AesMode mode, size_t bits)
{
    using Mode = mxb::Cipher::AesMode;

    const std::initializer_list<std::tuple<Mode, size_t, CipherFn>> ciphers =
    {
        {Mode::AES_CTR, 128, EVP_aes_128_ctr},
        {Mode::AES_CTR, 192, EVP_aes_192_ctr},
        {Mode::AES_CTR, 256, EVP_aes_256_ctr},

        {Mode::AES_CBC, 128, EVP_aes_128_cbc},
        {Mode::AES_CBC, 192, EVP_aes_192_cbc},
        {Mode::AES_CBC, 256, EVP_aes_256_cbc},

        {Mode::AES_GCM, 128, EVP_aes_128_gcm},
        {Mode::AES_GCM, 192, EVP_aes_192_gcm},
        {Mode::AES_GCM, 256, EVP_aes_256_gcm},

        {Mode::AES_CCM, 128, EVP_aes_128_ccm},
        {Mode::AES_CCM, 192, EVP_aes_192_ccm},
        {Mode::AES_CCM, 256, EVP_aes_256_ccm},
    };

    for (const auto& [m, b, fn] : ciphers)
    {
        if (m == mode && bits == b)
        {
            return fn;
        }
    }

    return nullptr;
}

static_assert(get_cipher_fn(mxb::Cipher::AES_CTR, 128) == EVP_aes_128_ctr);
static_assert(get_cipher_fn(mxb::Cipher::AES_CBC, 256) == EVP_aes_256_cbc);
static_assert(get_cipher_fn(mxb::Cipher::AES_GCM, 192) == EVP_aes_192_gcm);
static_assert(get_cipher_fn(mxb::Cipher::AES_CCM, 128) == EVP_aes_128_ccm);

const EVP_CIPHER* get_cipher(mxb::Cipher::AesMode mode, size_t bits)
{
    auto fn = get_cipher_fn(mode, bits);
    mxb_assert_message(fn, "Unknown cipher");
    MXB_ABORT_IF_NULL(fn);
    return fn();
}

std::vector<uint8_t> random_bytes(int size)
{
    std::vector<uint8_t> key(size);

    // Generate random bytes using OpenSSL.
    if (RAND_bytes(key.data(), size) != 1)
    {
        key.clear();
    }

    return key;
}
}

namespace maxbase
{

bool Cipher::encrypt_or_decrypt(const EVP_CIPHER* cipher, int enc,
                                const uint8_t* key, const uint8_t* iv,
                                const uint8_t* input, int input_len,
                                uint8_t* output, int* output_len)
{
    bool ok = false;

    if (EVP_CipherInit_ex(m_ctx, cipher, nullptr, key, iv, enc) == 1)
    {
        int output_written = 0;
        if (EVP_CipherUpdate(m_ctx, output, &output_written, input, input_len) == 1)
        {
            int total_output_len = output_written;
            if (EVP_CipherFinal_ex(m_ctx, output + total_output_len, &output_written) == 1)
            {
                total_output_len += output_written;
                *output_len = total_output_len;
                ok = true;
            }
        }
    }

    return ok;
}

bool Cipher::encrypt(const uint8_t* key, const uint8_t* iv,
                     const uint8_t* in, int in_len,
                     uint8_t* out, int* out_len)
{
    return encrypt_or_decrypt(m_cipher, ENCRYPTING, key, iv, in, in_len, out, out_len);
}

bool Cipher::decrypt(const uint8_t* key, const uint8_t* iv,
                     const uint8_t* in, int in_len,
                     uint8_t* out, int* out_len)
{
    return encrypt_or_decrypt(m_cipher, DECRYPTING, key, iv, in, in_len, out, out_len);
}

// static
void Cipher::log_errors(const char* operation)
{
    // It's unclear how thread(unsafe) OpenSSL error functions are. Minimize such possibilities by
    // using a local buffer.
    constexpr size_t bufsize = 256;     // Should be enough according to some googling.
    char buf[bufsize];
    buf[0] = '\0';

    auto errornum = ERR_get_error();
    auto errornum2 = ERR_get_error();
    ERR_error_string_n(errornum, buf, bufsize);

    if (errornum2 == 0)
    {
        // One error.
        MXB_ERROR("OpenSSL error %s. %s", operation, buf);
    }
    else
    {
        // Multiple errors, print all as separate messages.
        MXB_ERROR("Multiple OpenSSL errors %s. Detailed messages below.", operation);
        MXB_ERROR("%s", buf);
        while (errornum2 != 0)
        {
            ERR_error_string_n(errornum2, buf, bufsize);
            MXB_ERROR("%s", buf);
            errornum2 = ERR_get_error();
        }
    }
}

Cipher::Cipher(AesMode mode, size_t bits)
    : Cipher(get_cipher(mode, bits))
{
}

Cipher::Cipher(const EVP_CIPHER* cipher)
    : m_ctx(EVP_CIPHER_CTX_new())
    , m_cipher(cipher)
{
}

Cipher::~Cipher()
{
    EVP_CIPHER_CTX_free(m_ctx);
}

std::vector<uint8_t> Cipher::new_key() const
{
    auto key = random_bytes(key_size());

    if (key.empty())
    {
        log_errors("when creating new encryption key");
    }

    return key;
}

std::vector<uint8_t> Cipher::new_iv() const
{
    auto iv = random_bytes(iv_size());

    if (iv.empty())
    {
        log_errors("when creating new initialization vector");
    }

    return iv;
}

size_t Cipher::block_size() const
{
    return EVP_CIPHER_block_size(m_cipher);
}

size_t Cipher::iv_size() const
{
    return EVP_CIPHER_iv_length(m_cipher);
}

size_t Cipher::key_size() const
{
    return EVP_CIPHER_key_length(m_cipher);
}
}
