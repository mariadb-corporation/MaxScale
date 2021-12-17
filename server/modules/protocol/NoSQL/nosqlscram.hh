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
#pragma once

#include "nosqlprotocol.hh"
#include "nosqlbase.hh"

namespace nosql
{

namespace scram
{

constexpr int32_t SHA_1_HASH_SIZE = 20;
constexpr int32_t SHA_256_HASH_SIZE = 32;

constexpr int32_t SERVER_NONCE_SIZE = 24;
constexpr int32_t SERVER_SALT_SIZE = 16;

constexpr int32_t ITERATIONS = 4096;

std::vector<uint8_t> create_random_vector(size_t size);

void hmac_sha_1(const uint8_t* pKey, size_t key_len, const uint8_t* pData, size_t data_len, uint8_t* pOut);

std::vector<uint8_t> hmac_sha_1(const uint8_t* pKey, size_t key_len, const uint8_t* pData, size_t data_len);

inline std::vector<uint8_t> hmac_sha_1(const std::vector<uint8_t>& key, const char* zData)
{
    return hmac_sha_1(key.data(), key.size(), reinterpret_cast<const uint8_t*>(zData), strlen(zData));
}

inline std::vector<uint8_t> hmac_sha_1(const std::vector<uint8_t>& key, const std::string& data)
{
    return hmac_sha_1(key.data(), key.size(), reinterpret_cast<const uint8_t*>(data.data()), data.length());
}

void md5(const void* pData, size_t data_len, uint8_t* pOut);

void md5hex(const void* pData, size_t data_len, char* pOut);

std::string md5hex(const void* pData, size_t data_len);

std::vector<uint8_t> sha_1(const uint8_t* pData, size_t data_len);

inline std::vector<uint8_t> sha_1(const std::vector<uint8_t>& data)
{
    return sha_1(data.data(), data.size());
}

void pbkdf2_sha_1(const char* pPassword, size_t password_len,
                  const uint8_t* pSalt, size_t salt_len,
                  size_t iterations,
                  uint8_t* pOut);

std::vector<uint8_t> pbkdf2_sha_1(const char* pPassword, size_t password_len,
                                  const uint8_t* pSalt, size_t salt_len,
                                  size_t iterations);


inline std::vector<uint8_t> pbkdf2_sha_1(const std::string& password,
                                         const std::vector<uint8_t>& salt,
                                         size_t iterations)
{
    return pbkdf2_sha_1(password.data(), password.length(),
                        salt.data(), salt.size(),
                        iterations);
}

}

}
