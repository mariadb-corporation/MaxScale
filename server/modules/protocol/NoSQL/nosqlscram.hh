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

enum class Mechanism
{
    SHA_1,
    SHA_256
};

std::set<Mechanism> supported_mechanisms();

const char* to_string(Mechanism mechanism);
bool from_string(const std::string& mechanism, Mechanism* pMechanism);
inline bool from_string(const char* zMechanism, Mechanism* pMechanism)
{
    return from_string(std::string(zMechanism), pMechanism);
}
inline bool from_string(const string_view& mechanism, Mechanism* pMechanism)
{
    return from_string(std::string(mechanism.data(), mechanism.length()), pMechanism);
}

std::string to_json(const std::vector<Mechanism>& mechanisms);
bool from_json(const std::string& json, std::vector<Mechanism>* pMechanisms);

// throws if invalid.
void from_bson(const bsoncxx::array::view& bson, std::vector<Mechanism>* pRoles);

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
