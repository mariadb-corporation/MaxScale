/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <vector>

/**
 * @file include/maxscale/secrets.h - MaxScale config file password decryption
 */

#include <maxscale/ccdefs.hh>

namespace maxscale
{

constexpr size_t SECRETS_CIPHER_BYTES = 32;

/**
 * Encrypts a plaintext password using the currently loaded encryption key.
 *
 * @param input The password to encrypt
 *
 * @return The encrypted password
 */
std::string encrypt_password(const std::string& input);

/**
 * Encrypts string using provided key.
 *
 * @param key    The symmetric key. Its length must be @c SECRETS_CIPHER_BYTES.
 * @param input  The string to be encrypted.
 *
 * @return @c input encrypted using @c key, prefixed with the used initialization vector.
 *         Use @decrypt_password() for decrypting the string as it knows how to extract
 *         the initialization vector.
 */
std::string encrypt_password(const std::vector<uint8_t>& key, const std::string& input);

/**
 * Decrypts string using provided key.
 *
 * @param key    The symmetric key. Its length must be @c SECRETS_CIPHER_BYTES.
 * @param input  The string to be decrypted. *Must* have been created using
 *               @c encrypt_password() so that the string contains the needed
 *               initialization vector.
 *
 * @return @c input decrypted using @c key.
 */
std::string decrypt_password(const std::vector<uint8_t>& key, const std::string& input);

/**
 * Decrypt an encrypted password using the key loaded at startup. If the password is not encrypted,
 * ie is not a HEX string, return the original.
 *
 * @param input The encrypted password
 * @return The decrypted password.
 */
std::string decrypt_password(const std::string& input);

}
