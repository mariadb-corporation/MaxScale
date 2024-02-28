/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <array>

namespace postgres
{
// Hash size, 32 bytes for SHA-256.
constexpr static size_t SCRAM_HASH_SIZE = 32;

// The salt length used by Postgres. It seems to be half the hash size.
constexpr static size_t SCRAM_SALT_SIZE = 16;

// Postgres uses a hard-coded iteration count of 4096.
constexpr static size_t SCRAM_ITER_COUNT = 4096;

// The SCRAM secrets in binary form. The names are from the SCRAM-SHA-256 authentication specification except
// they were converted to snake_case.
struct ScramSecrets
{
    std::array<uint8_t, SCRAM_HASH_SIZE> salted_pw{};
    std::array<uint8_t, SCRAM_HASH_SIZE> client_key{};
    std::array<uint8_t, SCRAM_HASH_SIZE> server_key{};
    std::array<uint8_t, SCRAM_HASH_SIZE> stored_key{};
};

using ScramSalt = std::array<uint8_t, SCRAM_SALT_SIZE>;

/**
 * Get SCRAM-SHA-256 secrets from a password and a salt
 *
 * @param pw   Plaintext password
 * @param salt The salt that was used
 *
 * @return The SCRAM-SHA-256 secrets
 */
ScramSecrets get_scram_secrets(const std::string& pw, const ScramSalt& salt);

/**
 * Salt a password into Postgres storage format
 *
 * Converts a plaintext password (in UTF-8) into a salted password and then formats it in a way that Postgres
 * understands. The generated password string is identical to the `rolpassword` field in the `pg_authid`
 * table.
 *
 * The salt will be randomly generated which means verification of the password should use get_scram_secrets.
 *
 * @param pw   The plaintext password
 *
 * @return The salted password in Postgres format
 */
std::string salt_password(const std::string& pw);
}

namespace pg = postgres;
