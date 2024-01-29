/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <string>
#include <tuple>

namespace maxscale
{
namespace jwt
{

/**
 * Create a JSON Web Token
 *
 * The JSON Web Token standardizes a handful of values from which the audience ("sub") is the most suitable
 * for uniquely identifying a user. Custom values could be added but, for the sake simplicity, we only use the
 * one standard value to store the actual user information.
 *
 * @param issuer   The issuer of this token (e.g. maxscale)
 * @param subject  The recipient of the token. The information stored here is not encrypted so don't store
 *                 anything sensitive in it.
 * @param max_age  The age in seconds the token is valid for.
 *
 * @return A JSON web token in the encoded format.
 */
std::string create(const std::string& issuer, const std::string& subject, int max_age);

/**
 * Extract the subject value from a JSON Web Token
 *
 * The function checks that the issuer of the token is the same as the one who created it.
 *
 * @param issuer The issuer who created this token
 * @param token  A JWT token that is validated before the subject value is extracted
 *
 * @return A boolean indicating whether the token is valid and the subject value if it is
 */
std::pair<bool, std::string> get_subject(const std::string& issuer, const std::string& token);
}
}
