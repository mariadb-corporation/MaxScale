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
#include <maxbase/filesystem.hh>
#include <maxscale/config.hh>

namespace
{

std::string rand_key(int bits)
{
    std::string key;
    key.resize(bits / 8);
    RAND_bytes((uint8_t*)key.data(), key.size());
    return key;
}

// Abstract base class for signature creation and verification
struct Jwt
{
    virtual ~Jwt() = default;

    virtual std::string sign(const std::string& issuer, const std::string& audience, int max_age) = 0;

    virtual std::pair<bool, std::string>
    get_audience(const std::string& issuer, const std::string& token) = 0;
};

template<class Algorithm>
class JwtBase : public Jwt
{
public:
    JwtBase(Algorithm algo)
        : m_algo(std::move(algo))
    {
    }

    std::string sign(const std::string& issuer,
                     const std::string& audience,
                     int max_age) override
    {
        auto now = std::chrono::system_clock::now();

        return ::jwt::create()
               .set_issuer(issuer)
               .set_audience(audience)
               .set_issued_at(now)
               .set_expires_at(now + std::chrono::seconds {max_age})
               .sign(m_algo);
    }

    std::pair<bool, std::string>
    get_audience(const std::string& issuer, const std::string& token) override
    {
        bool rval = false;
        std::string audience;

        try
        {
            auto d = ::jwt::decode(token) ;

            ::jwt::verify()
            .allow_algorithm(m_algo)
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

private:
    Algorithm m_algo;
};

template<class Algorithm>
std::unique_ptr<Jwt> make_jwt(Algorithm algo)
{
    return std::make_unique<JwtBase<Algorithm>>(std::move(algo));
}

bool is_pubkey_alg(mxs::JwtAlgo algo)
{
    switch (algo)
    {
    case mxs::JwtAlgo::HS256:
    case mxs::JwtAlgo::HS384:
    case mxs::JwtAlgo::HS512:
        return false;

    default:
        return true;
    }
}

void check_key(const std::string& key, size_t bits)
{
    if (!key.empty() && key.size() * 8 < bits)
    {
        throw std::runtime_error(MAKE_STR("Key is too small, need at least a" << bits << "-bit key."));
    }
}

struct ThisUnit
{
    std::unique_ptr<Jwt> jwt;
};

ThisUnit this_unit;
}

namespace maxscale
{
namespace jwt
{

bool init()
{
    const auto& cnf = mxs::Config::get();
    std::unique_ptr<Jwt> jwt;
    std::string key;
    std::string cert;
    std::string err;

    if (is_pubkey_alg(cnf.admin_jwt_algorithm))
    {
        if (std::tie(key, err) = mxb::load_file<std::string>(cnf.admin_ssl_key); !err.empty())
        {
            MXB_ERROR("Failed to load REST API private key: %s", err.c_str());
            return false;
        }
        else if (std::tie(cert, err) = mxb::load_file<std::string>(cnf.admin_ssl_cert); !err.empty())
        {
            MXB_ERROR("Failed to load REST API public certificate: %s", err.c_str());
            return false;
        }
    }
    else if (!cnf.admin_jwt_key.empty())
    {
        auto km = mxs::key_manager();
        mxb_assert(km);

        if (auto [ok, vers, binkey] = km->get_key(cnf.admin_jwt_key); ok)
        {
            key.assign(binkey.begin(), binkey.end());
        }
        else
        {
            MXB_ERROR("Could not load JWT signature key '%s'", cnf.admin_jwt_key.c_str());
            return false;
        }
    }

    try
    {
        switch (cnf.admin_jwt_algorithm)
        {
        case mxs::JwtAlgo::HS256:
            check_key(key, 256);
            jwt = make_jwt(::jwt::algorithm::hs256 {key.empty() ? rand_key(256) : key});
            break;

        case mxs::JwtAlgo::HS384:
            check_key(key, 384);
            jwt = make_jwt(::jwt::algorithm::hs384 {key.empty() ? rand_key(384) : key});
            break;

        case mxs::JwtAlgo::HS512:
            check_key(key, 512);
            jwt = make_jwt(::jwt::algorithm::hs512 {key.empty() ? rand_key(512) : key});
            break;

        case mxs::JwtAlgo::RS256:
            jwt = make_jwt(::jwt::algorithm::rs256 {cert, key});
            break;

        case mxs::JwtAlgo::RS384:
            jwt = make_jwt(::jwt::algorithm::rs384 {cert, key});
            break;

        case mxs::JwtAlgo::RS512:
            jwt = make_jwt(::jwt::algorithm::rs512 {cert, key});
            break;

        case mxs::JwtAlgo::ES256:
            jwt = make_jwt(::jwt::algorithm::es256 {cert, key});
            break;

        case mxs::JwtAlgo::ES384:
            jwt = make_jwt(::jwt::algorithm::es384 {cert, key});
            break;

        case mxs::JwtAlgo::ES512:
            jwt = make_jwt(::jwt::algorithm::es512 {cert, key});
            break;

        case mxs::JwtAlgo::PS256:
            jwt = make_jwt(::jwt::algorithm::ps256 {cert, key});
            break;

        case mxs::JwtAlgo::PS384:
            jwt = make_jwt(::jwt::algorithm::ps384 {cert, key});
            break;

        case mxs::JwtAlgo::PS512:
            jwt = make_jwt(::jwt::algorithm::ps512 {cert, key});
            break;

        case mxs::JwtAlgo::ED25519:
#ifdef OPENSSL_1_1
            jwt = make_jwt(::jwt::algorithm::ed25519 {cert, key});
#else
            MXB_ERROR("ED25519 is not supported on this system.");
#endif
            break;

        case mxs::JwtAlgo::ED448:
#ifdef OPENSSL_1_1
            jwt = make_jwt(::jwt::algorithm::ed448 {cert, key});
#else
            MXB_ERROR("ED448 is not supported on this system.");
#endif
            break;

        default:
            mxb_assert_message(!true, "We shouldn't end up here.");
            break;
        }

        this_unit.jwt = std::move(jwt);
    }
    catch (const std::exception& e)
    {
        MXB_ERROR("Key initialization failed: %s", e.what());
    }

    return this_unit.jwt.get();
}

std::string create(const std::string& issuer, const std::string& audience, int max_age)
{
    return this_unit.jwt->sign(issuer, audience, max_age);
}

std::pair<bool, std::string> get_audience(const std::string& issuer, const std::string& token)
{
    return this_unit.jwt->get_audience(issuer, token);
}
}
}
