/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/secrets.hh>
#include <maxbase/log.hh>
#include <maxbase/assert.hh>
#include <maxbase/alloc.hh>
#include <maxbase/string.hh>

#include <tuple>
#include <initializer_list>

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/evp.h>

#define ENCRYPTING 1
#define DECRYPTING 0

namespace
{

constexpr const int GCM_AAD_OFFSET = 12;
constexpr const int GCM_AAD_SIZE = 4;

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
    if (bits != 128 && bits != 192 && bits != 256)
    {
        MXB_THROW(mxb::KeySizeException, "Invalid key size: " << bits << " bits");
    }

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

std::string get_openssl_errors()
{
    std::vector<std::string> errors;

    while (auto errornum = ERR_get_error())
    {
        // It's unclear how thread(un)safe OpenSSL error functions are. Minimize such possibilities by
        // using a local buffer. The 256 bytes should be enough according to some googling.
        char buf[256] {0};
        ERR_error_string_n(errornum, buf, sizeof(buf));
        errors.push_back(buf);
    }

    return mxb::join(errors, "; ");
}

bool Cipher::encrypt_or_decrypt(const EVP_CIPHER* cipher, int enc,
                                const uint8_t* key, const uint8_t* iv,
                                const uint8_t* input, int input_len,
                                uint8_t* output, int* output_len)
{
    bool ok = false;
    int mode = EVP_CIPHER_mode(cipher);
    bool is_gcm = mode == EVP_CIPH_GCM_MODE;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();

    if (EVP_CipherInit_ex(ctx, cipher, nullptr, key, iv, enc) == 1)
    {
        if (is_gcm)
        {
            // Use the last 4 bytes of the IV as the Additional Authenticated Data
            set_aad(ctx, iv + GCM_AAD_OFFSET, GCM_AAD_SIZE);
            mxb_assert(EVP_CIPHER_iv_length(cipher) == 12);
        }

        if (is_gcm && enc == DECRYPTING)
        {
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)(input + input_len - 16));
            input_len -= 16;
        }

        int output_written = 0;
        if (EVP_CipherUpdate(ctx, output, &output_written, input, input_len) == 1)
        {
            int total_output_len = output_written;
            if (EVP_CipherFinal_ex(ctx, output + total_output_len, &output_written) == 1)
            {
                total_output_len += output_written;

                if (is_gcm && enc == ENCRYPTING)
                {
                    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, output + total_output_len);
                    total_output_len += 16;
                }

                *output_len = total_output_len;
                ok = true;
            }
        }
    }

    EVP_CIPHER_CTX_free(ctx);

    return ok;
}

void Cipher::set_aad(EVP_CIPHER_CTX* ctx, const uint8_t* ptr, size_t len)
{
    int dummy;
    EVP_CipherUpdate(ctx, nullptr, &dummy, ptr, len);
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
    MXB_ERROR("OpenSSL error %s. %s", operation, get_errors().c_str());
}

// static
std::string Cipher::get_errors()
{
    return get_openssl_errors();
}

Cipher::Cipher(AesMode mode, size_t bits)
    : Cipher(get_cipher(mode, bits))
{
}

Cipher::Cipher(const EVP_CIPHER* cipher)
    : m_cipher(cipher)
{
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
    size_t sz = EVP_CIPHER_iv_length(m_cipher);

    if (EVP_CIPHER_mode(m_cipher) == EVP_CIPH_GCM_MODE)
    {
        // Store the AAD in the last 4 bytes of the IV. As the AES-GCM mode is limited to a 12-byte IV, this
        // is a convenient way of having extra verification.
        sz += 4;
        mxb_assert_message(sz == 16, "AES-GCM IV must be 16 bytes");
    }

    return sz;
}

size_t Cipher::key_size() const
{
    return EVP_CIPHER_key_length(m_cipher);
}

size_t Cipher::encrypted_size(size_t len) const
{
    switch (EVP_CIPHER_mode(m_cipher))
    {
    case EVP_CIPH_CBC_MODE:
        {
            // The data is padded to a multiple of the block size. If data is already a multiple of a block
            // size, an extra block is added.
            size_t bs = block_size();
            return ((len + bs) / bs) * bs;
        }

    case EVP_CIPH_CTR_MODE:
        return len;

    case EVP_CIPH_GCM_MODE:
        return len + 16;

    default:
        mxb_assert(!true);
        return len;
    }
}

std::string Cipher::to_string()const
{
    std::string mode;

    switch (EVP_CIPHER_mode(m_cipher))
    {
    case EVP_CIPH_CBC_MODE:
        mode = "AES_CBC_";
        break;

    case EVP_CIPH_CTR_MODE:
        mode = "AES_CTR_";
        break;

    case EVP_CIPH_GCM_MODE:
        mode = "AES_GCM_";
        break;

    default:
        mode = "UNKNOWN_";
        break;
    }

    return mode + std::to_string(key_size() * 8);
}
}
