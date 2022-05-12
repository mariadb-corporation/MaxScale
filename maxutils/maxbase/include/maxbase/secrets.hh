/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxbase/ccdefs.hh>

#include <cstring>

#include <openssl/evp.h>

namespace maxbase
{
class Cipher
{
public:

    enum class Mode
    {
        ENCRYPT,
        DECRYPT,
    };

    Cipher(const Cipher&) = delete;
    Cipher& operator=(const Cipher&) = delete;

    Cipher(Cipher&&) = default;
    Cipher& operator=(Cipher&&) = default;

    /**
     * Creates a new Cipher
     *
     * @param cipher The EVP cipher to use
     * @param key    The encryption key
     * @param iv     The initialization vector
     */
    Cipher(const EVP_CIPHER* cipher, const uint8_t* key, const uint8_t* iv)
        : m_ctx(EVP_CIPHER_CTX_new())
        , m_cipher(cipher)
    {
        memcpy(m_key, key, EVP_CIPHER_key_length(cipher));
        memcpy(m_iv, iv, EVP_CIPHER_iv_length(cipher));
    }

    ~Cipher()
    {
        EVP_CIPHER_CTX_free(m_ctx);
    }

    /**
     * Encrypt or decrypt the input buffer to output buffer.
     *
     * @param mode Encrypting or decrypting
     * @param input Input buffer
     * @param input_len Input length
     * @param output Output buffer
     * @param output_len Produced output length is written here
     *
     * @return True on success
     */
    bool encrypt_or_decrypt(Mode mode, const uint8_t* input, int input_len,
                            uint8_t* output, int* output_len);

    // Helper for encrypting
    bool encrypt(const uint8_t* in, int in_len, uint8_t* out, int* out_len)
    {
        return encrypt_or_decrypt(Mode::ENCRYPT, in, in_len, out, out_len);
    }

    // Helper for decrypting
    bool decrypt(const uint8_t* in, int in_len, uint8_t* out, int* out_len)
    {
        return encrypt_or_decrypt(Mode::DECRYPT, in, in_len, out, out_len);
    }

    /**
     * Log encryption errors
     *
     * @param operation The operation being taken, logged as a part of the error
     */
    void log_errors(const char* operation);

private:
    EVP_CIPHER_CTX*   m_ctx;
    const EVP_CIPHER* m_cipher;
    uint8_t           m_key[EVP_MAX_KEY_LENGTH];
    uint8_t           m_iv[EVP_MAX_IV_LENGTH];
};
}
