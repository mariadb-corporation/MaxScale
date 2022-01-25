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

#include "nosqlcrypto.hh"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <maxbase/worker.hh>

using namespace std;

namespace nosql
{

static_assert(NOSQL_MD5_DIGEST_LENGTH == MD5_DIGEST_LENGTH);
static_assert(NOSQL_SHA_DIGEST_LENGTH == SHA_DIGEST_LENGTH);

vector<uint8_t> crypto::create_random_bytes(size_t size)
{
    vector<uint8_t> rv(size);

    mxb::Worker::gen_random_bytes(rv.data(), rv.size());

    return rv;
}

//
// HMAC SHA 1
//
void crypto::hmac_sha_1(const uint8_t* pKey, size_t key_len,
                        const uint8_t* pData, size_t data_len,
                        uint8_t* pOut)
{
    HMAC(EVP_sha1(), pKey, key_len, pData, data_len, pOut, nullptr);
}

vector<uint8_t> crypto::hmac_sha_1(const uint8_t* pKey, size_t key_len, const uint8_t* pData, size_t data_len)
{
    vector<uint8_t> rv(NOSQL_SHA_1_HASH_SIZE);

    hmac_sha_1(pKey, key_len, pData, data_len, rv.data());

    return rv;
}

//
// HMAC SHA 256
//
void crypto::hmac_sha_256(const uint8_t* pKey, size_t key_len,
                          const uint8_t* pData, size_t data_len,
                          uint8_t* pOut)
{
    HMAC(EVP_sha256(), pKey, key_len, pData, data_len, pOut, nullptr);
}

vector<uint8_t> crypto::hmac_sha_256(const uint8_t* pKey, size_t key_len,
                                     const uint8_t* pData, size_t data_len)
{
    vector<uint8_t> rv(NOSQL_SHA_256_HASH_SIZE);

    hmac_sha_256(pKey, key_len, pData, data_len, rv.data());

    return rv;
}

//
// SHA 1
//
std::vector<uint8_t> crypto::sha_1(const uint8_t* pData, size_t data_len)
{
    vector<uint8_t> rv(NOSQL_SHA_DIGEST_LENGTH);

    SHA1(pData, data_len, rv.data());

    return rv;
}

//
// SHA 256
//
std::vector<uint8_t> crypto::sha_256(const uint8_t* pData, size_t data_len)
{
    vector<uint8_t> rv(NOSQL_SHA_DIGEST_LENGTH);

    SHA256(pData, data_len, rv.data());

    return rv;
}

//
// MD5
//
void crypto::md5(const void* pData, size_t data_len, uint8_t* pOut)
{
    MD5(reinterpret_cast<const uint8_t*>(pData), data_len, pOut);
}

void crypto::md5hex(const void* pData, size_t data_len, char* pOut)
{
    uint8_t digest[NOSQL_MD5_DIGEST_LENGTH];
    md5(pData, data_len, digest);

    for (size_t i = 0; i < sizeof(digest); ++i) {
        snprintf(&pOut[i * 2], 3, "%02x", digest[i]);
    }

    // TODO: mxs::bin2hex() can't be used as it uses uppercase and SCRAM requires lowercase.
}

std::string crypto::md5hex(const void* pData, size_t data_len)
{
    char str[2 * NOSQL_MD5_DIGEST_LENGTH + 1];
    md5hex(pData, data_len, str);
    return str;
}

}
