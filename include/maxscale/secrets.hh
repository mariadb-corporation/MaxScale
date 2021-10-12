/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file include/maxscale/secrets.h - MaxScale config file password decryption
 */

#include <maxscale/ccdefs.hh>

namespace maxscale
{
/**
 * Decrypt an encrypted password using the key loaded at startup. If the password is not encrypted,
 * ie is not a HEX string, return the original.
 *
 * @param input The encrypted password
 * @return The decrypted password.
 */
std::string decrypt_password(const std::string& input);
}
