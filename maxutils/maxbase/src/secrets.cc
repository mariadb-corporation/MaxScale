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

#include <openssl/err.h>

#define ENCRYPTING 1
#define DECRYPTING 0

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

Cipher::Cipher(const EVP_CIPHER* cipher)
    : m_ctx(EVP_CIPHER_CTX_new())
    , m_cipher(cipher)
{
}

Cipher::~Cipher()
{
    EVP_CIPHER_CTX_free(m_ctx);
}
}
