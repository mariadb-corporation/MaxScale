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
#include <vector>

#include <openssl/evp.h>

namespace maxbase
{
class Cipher
{
public:

    enum AesMode
    {
        AES_CTR,
        AES_CBC,
        AES_GCM,    // TODO: Implement
        AES_CCM,    // TODO: Implement
    };

    Cipher(const Cipher&) = delete;
    Cipher& operator=(const Cipher&) = delete;

    Cipher(Cipher&&) = default;
    Cipher& operator=(Cipher&&) = default;

    /**
     * Creates a new Cipher
     *
     * @param mode The AES cipher mode to use
     * @param bits How many bits to use for keys. Must be one of 128, 192 or 256.
     */
    Cipher(AesMode mode, size_t bits);

    /**
     * Creates a new Cipher using a custom EVP cipher
     *
     * @param cipher The EVP cipher to use
     *
     * @note Use this if there's no constant for the given cipher.
     */
    Cipher(const EVP_CIPHER* cipher);

    ~Cipher();

    /**
     * Get cipher block size
     *
     * @return Block size in bytes
     */
    size_t block_size() const;

    /**
     * Get cipher IV size
     *
     * @return IV size in bytes
     */
    size_t iv_size() const;

    /**
     * Get cipher key size
     *
     * @return Key size inin bytes
     */
    size_t key_size() const;

    /**
     * Creates a new encryption key
     *
     * @return The new encryption key
     */
    std::vector<uint8_t> new_key() const;

    /**
     * Creates a new initialization vector
     *
     * @return The new initialization vector
     */
    std::vector<uint8_t> new_iv() const;

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
    static void log_errors(const char* operation);

private:

    bool encrypt_or_decrypt(const EVP_CIPHER* cipher, int enc,
                            const uint8_t* key, const uint8_t* iv,
                            const uint8_t* input, int input_len,
                            uint8_t* output, int* output_len);

    EVP_CIPHER_CTX*   m_ctx;
    const EVP_CIPHER* m_cipher;
};
}
