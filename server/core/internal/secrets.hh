/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

/**
 * @file core/maxscale/secrets.h - MaxScale config file password encryption/decryption
 */

#include <maxscale/secrets.hh>
#include <maxbase/secrets.hh>
#include <memory>

using ByteVec = std::vector<uint8_t>;

extern const char* const SECRETS_FILENAME;

// Don't change these: they must be 256-bit AES CBC to support passwords created with MaxScale 2.5.
constexpr auto SECRETS_CIPHER_MODE = mxb::Cipher::AES_CBC;
constexpr size_t SECRETS_CIPHER_BITS = 256;

bool load_encryption_keys();

struct ReadKeyResult
{
    bool    ok {false};
    ByteVec key;
};

ReadKeyResult secrets_readkeys(const std::string& filepath);
bool          secrets_write_keys(const ByteVec& key, const std::string& filepath, const std::string& owner);
