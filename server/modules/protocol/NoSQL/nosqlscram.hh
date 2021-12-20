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
#include "nosqlcrypto.hh"

namespace nosql
{

namespace scram
{

constexpr int32_t SERVER_NONCE_SIZE = 24;
constexpr int32_t SERVER_SALT_SIZE = 16;

constexpr int32_t ITERATIONS = 4096;

void pbkdf2_hmac_sha_1(const char* pPassword, size_t password_len,
                       const uint8_t* pSalt, size_t salt_len,
                       size_t iterations,
                       uint8_t* pOut);

std::vector<uint8_t> pbkdf2_hmac_sha_1(const char* pPassword, size_t password_len,
                                       const uint8_t* pSalt, size_t salt_len,
                                       size_t iterations);


inline std::vector<uint8_t> pbkdf2_hmac_sha_1(const std::string& password,
                                              const std::vector<uint8_t>& salt,
                                              size_t iterations)
{
    return pbkdf2_hmac_sha_1(password.data(), password.length(),
                             salt.data(), salt.size(),
                             iterations);
}

}

}
