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

}

}
