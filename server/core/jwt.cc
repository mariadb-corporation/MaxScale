/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/jwt.hh"

#include <jwt-cpp/jwt.h>
#include <random>

#include <maxbase/assert.h>

namespace
{
struct ThisUnit
{
    ThisUnit()
    {
        // Initialize JWT signing key
        std::random_device gen;
        constexpr auto KEY_BITS = 512;
        constexpr auto VALUE_SIZE = sizeof(decltype(gen()));
        constexpr auto NUM_VALUES = KEY_BITS / VALUE_SIZE;
        std::vector<decltype(gen())> key;
        key.reserve(NUM_VALUES);
        std::generate_n(std::back_inserter(key), NUM_VALUES, std::ref(gen));
        sign_key.assign((const char*)key.data(), key.size() * VALUE_SIZE);
        mxb_assert(sign_key.size() == KEY_BITS);
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
           .sign(::jwt::algorithm::hs256 {this_unit.sign_key});
}

std::pair<bool, std::string> get_audience(const std::string& issuer, const std::string& token)
{
    bool rval = false;
    std::string audience;

    try
    {
        auto d = ::jwt::decode(token);

        ::jwt::verify()
        .allow_algorithm(::jwt::algorithm::hs256 {this_unit.sign_key})
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
