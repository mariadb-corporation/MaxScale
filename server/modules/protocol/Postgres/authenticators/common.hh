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
#include <openssl/sha.h>
#include <optional>
#include <array>

using Digest = std::array<uint8_t, SHA256_DIGEST_LENGTH>;

struct ScramUser
{
    std::string iter;
    std::string salt;
    Digest      stored_key{};
    Digest      server_key{};
};

std::optional<ScramUser> parse_scram_password(std::string_view pw);
