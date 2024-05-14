/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxbase/assert.hh>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

namespace maxscale
{
namespace jwt
{


class Claims
{
public:
    struct Imp
    {
        virtual ~Imp() = default;
        virtual std::optional<std::string> get(const std::string& name) = 0;
    };

    Claims(std::unique_ptr<Imp> imp)
        : m_imp(std::move(imp))
    {
    }

    /**
     * Get a claim from the token
     *
     * @param name The name of the claim
     *
     * @return The claim value, if found, formatted as a string
     */
    std::optional<std::string> get(const std::string& name)
    {
        mxb_assert(m_imp.get());
        return m_imp->get(name);
    }

private:
    std::unique_ptr<Imp> m_imp;
};

/**
 * Initialize JWT singing keys
 *
 * This function must be called after the global configuration has been read and before any of the other
 * functions are used.
 *
 * @return True if initialization was successful.
 */
bool init();

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
 * @param claims   Extra claims to be added to the token. These MUST NOT conflict with any well-known claims.
 *
 * @return A JSON web token in the encoded format.
 */
std::string create(const std::string& issuer, const std::string& subject, int max_age,
                   std::map<std::string, std::string> claims = {});

/**
 * Decode and validate a JSON Web Token
 *
 * The function checks that the issuer of the token is the same as the one who created it.
 *
 * @param issuer The issuer who created this token
 * @param token  A JWT token that is validated before the subject value is extracted
 *
 * @return The claims found in the token if decoding and validation was successful
 */
std::optional<Claims> decode(const std::string& issuer, const std::string& token);
}
}
