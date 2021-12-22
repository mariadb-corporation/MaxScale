/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqlscram.hh"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <maxbase/worker.hh>

using namespace std;

namespace nosql
{

void scram::pbkdf2_hmac_sha_1(const char* pPassword, size_t password_len,
                              const uint8_t* pSalt, size_t salt_len,
                              size_t iterations,
                              uint8_t* pOutput)
{
    uint8_t start_key[NOSQL_SHA_1_HASH_SIZE];

    memcpy(start_key, pSalt, salt_len);

    start_key[salt_len] = 0;
    start_key[salt_len + 1] = 0;
    start_key[salt_len + 2] = 0;
    start_key[salt_len + 3] = 1;

    crypto::hmac_sha_1(reinterpret_cast<const uint8_t*>(pPassword), password_len,
                       start_key, NOSQL_SHA_1_HASH_SIZE,
                       pOutput);

    uint8_t intermediate_digest[NOSQL_SHA_1_HASH_SIZE];

    memcpy(intermediate_digest, pOutput, NOSQL_SHA_1_HASH_SIZE);

    for (size_t i = 2; i <= iterations; ++i)
    {
        crypto::hmac_sha_1(reinterpret_cast<const uint8_t*>(pPassword), password_len,
                           intermediate_digest, NOSQL_SHA_1_HASH_SIZE,
                           intermediate_digest);

        for (int j = 0; j < NOSQL_SHA_1_HASH_SIZE; j++)
        {
            pOutput[j] ^= intermediate_digest[j];
        }
    }
}

vector<uint8_t> scram::pbkdf2_hmac_sha_1(const char* pPassword, size_t password_len,
                                         const uint8_t* pSalt, size_t salt_len,
                                         size_t iterations)
{
    vector<uint8_t> rv(NOSQL_SHA_1_HASH_SIZE);

    pbkdf2_hmac_sha_1(pPassword, password_len, pSalt, salt_len, iterations, rv.data());

    return rv;
}

}
