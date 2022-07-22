/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/jwt.hh"

#include <jwt-cpp/jwt.h>
#include <random>
#include <openssl/rand.h>

#include <maxbase/assert.hh>

namespace
{
struct ThisUnit
{
    ThisUnit()
    {
        // Initialize JWT signing key
        constexpr auto KEY_BITS = 512;
        sign_key.resize(KEY_BITS / 8);
        RAND_bytes((uint8_t*)sign_key.data(), sign_key.size());
    }

    std::string sign_key;
};

ThisUnit this_unit;
}

namespace maxscale
{
namespace jwt
{

std::string create(const std::string& issuer, const std::string& audience, int max_age)
{
    auto now = std::chrono::system_clock::now();

    return ::jwt::create()
           .set_issuer(issuer)
           .set_audience(audience)
           .set_issued_at(now)
           .set_expires_at(now + std::chrono::seconds {max_age})
           .sign(::jwt::algorithm::hs512 {this_unit.sign_key});
}

std::pair<bool, std::string> get_audience(const std::string& issuer, const std::string& token)
{
    bool rval = false;
    std::string audience;

    try
    {
        auto d = ::jwt::decode(token);

        ::jwt::verify()
        .allow_algorithm(::jwt::algorithm::hs512 {this_unit.sign_key})
        .with_issuer(issuer)
        .verify(d);

        audience = *d.get_audience().begin();
        rval = true;
    }
    catch (const std::exception& e)
    {
    }

    return {rval, audience};
}
}
}
