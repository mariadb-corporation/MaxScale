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

    Cipher(const Cipher&) = delete;
    Cipher& operator=(const Cipher&) = delete;

    Cipher(Cipher&&) = default;
    Cipher& operator=(Cipher&&) = default;

    /**
     * Creates a new Cipher
     *
     * @param cipher The EVP cipher to use
     */
    Cipher(const EVP_CIPHER* cipher);

    ~Cipher();

    /**
     * Encrypt the input buffer to output buffer.
     *
     * @param key        Encryption key to use, must be of the right size for the given cipher
     * @param iv         Initialization vector
     * @param input      Input buffer
     * @param input_len  Input length
     * @param output     Output buffer
     * @param output_len Produced output length is written here
     *
     * @return True on success
     */
    bool encrypt(const uint8_t* key, const uint8_t* iv,
                 const uint8_t* in, int in_len,
                 uint8_t* out, int* out_len);


    /**
     * Decrypt the input buffer to output buffer.
     *
     * @param key        Encryption key to use, must be of the right size for the given cipher
     * @param iv         Initialization vector
     * @param input      Input buffer
     * @param input_len  Input length
     * @param output     Output buffer
     * @param output_len Produced output length is written here
     *
     * @return True on success
     */
    bool decrypt(const uint8_t* key, const uint8_t* iv,
                 const uint8_t* in, int in_len,
                 uint8_t* out, int* out_len);

    /**
     * Log encryption errors
     *
     * @param operation The operation being taken, logged as a part of the error
     */
    void log_errors(const char* operation);

private:

    bool encrypt_or_decrypt(const EVP_CIPHER* cipher, int enc,
                            const uint8_t* key, const uint8_t* iv,
                            const uint8_t* input, int input_len,
                            uint8_t* output, int* output_len);

    EVP_CIPHER_CTX*   m_ctx;
    const EVP_CIPHER* m_cipher;
};
}
