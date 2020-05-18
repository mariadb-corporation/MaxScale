/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-04-23
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

/**
 * The key structure held in the secrets file
 */
struct EncryptionKeys
{
    static constexpr int key_len = 32;      // For AES256
    static constexpr int iv_len = 16;
    static constexpr int total_len = key_len + iv_len;

    unsigned char enckey[key_len] {0};
    unsigned char initvector[iv_len] {0};
};

int         secrets_write_keys(const std::string& directory);
std::string encrypt_password(const char*, const char*);
bool        load_encryption_keys();
