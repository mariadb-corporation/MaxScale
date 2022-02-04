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

constexpr int32_t NOSQL_SHA_1_HASH_SIZE = 20;
constexpr int32_t NOSQL_SHA_256_HASH_SIZE = 32;

constexpr int32_t NOSQL_MD5_DIGEST_LENGTH = 16;
constexpr int32_t NOSQL_SHA_DIGEST_LENGTH = 20;
constexpr int32_t NOSQL_SHA256_DIGEST_LENGTH = 32;

namespace crypto
{

std::vector<uint8_t> create_random_bytes(size_t size);

//
// HMAC SHA 1
//
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

//
// HMAC SHA 256
//
void hmac_sha_256(const uint8_t* pKey, size_t key_len, const uint8_t* pData, size_t data_len, uint8_t* pOut);

std::vector<uint8_t> hmac_sha_256(const uint8_t* pKey, size_t key_len, const uint8_t* pData, size_t data_len);

inline std::vector<uint8_t> hmac_sha_256(const std::vector<uint8_t>& key, const char* zData)
{
    return hmac_sha_256(key.data(), key.size(), reinterpret_cast<const uint8_t*>(zData), strlen(zData));
}

inline std::vector<uint8_t> hmac_sha_256(const std::vector<uint8_t>& key, const std::string& data)
{
    return hmac_sha_256(key.data(), key.size(), reinterpret_cast<const uint8_t*>(data.data()), data.length());
}

//
// MD5
//
void md5(const void* pData, size_t data_len, uint8_t* pOut);

void md5hex(const void* pData, size_t data_len, char* pOut);

std::string md5hex(const void* pData, size_t data_len);

inline std::string md5hex(const std::string& s)
{
    return md5hex(s.data(), s.length());
}

//
// SHA 1
//
std::vector<uint8_t> sha_1(const uint8_t* pData, size_t data_len);

inline std::vector<uint8_t> sha_1(const std::vector<uint8_t>& data)
{
    return sha_1(data.data(), data.size());
}

inline std::vector<uint8_t> sha_1(const std::string& s)
{
    return sha_1(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

//
// SHA 256
//
std::vector<uint8_t> sha_256(const uint8_t* pData, size_t data_len);

inline std::vector<uint8_t> sha_256(const std::vector<uint8_t>& data)
{
    return sha_256(data.data(), data.size());
}

}

}
