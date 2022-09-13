/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/jwt.hh"

#include <jwt-cpp/jwt.h>
#include <random>
#include <openssl/rand.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#include <maxbase/assert.hh>
#include <maxbase/filesystem.hh>
#include <maxscale/config.hh>

namespace
{

// Uncrustify doesn't like a leading :: when used with a template parameter and breaks unless there's a space
// between the < and the ::. A custom namespace works around this problem and it also makes it slightly more
// readable.
namespace Sig = ::jwt::algorithm;

std::string rand_key(int bits)
{
    std::string key;
    key.resize(bits / 8);
    RAND_bytes((uint8_t*)key.data(), key.size());
    return key;
}

std::string cert_cleanup(std::string cert)
{
    // Remove any extra data that might be at the beginning of the certificate. The jwt-cpp library doesn't
    // like it and ends up throwing errors for perfectly valid certificates that would otherwise be accepted
    // by OpenSSL.

    auto pos = cert.find("-----BEGIN ");

    if (pos != std::string::npos)
    {
        auto first_line = cert.find_last_of('\n', pos);

        if (first_line != std::string::npos)
        {
            cert = cert.substr(first_line + 1);
        }
    }

    return cert;
}

// Abstract base class for signature creation and verification
struct Jwt
{
    virtual ~Jwt() = default;

    virtual std::string sign(const std::string& issuer, const std::string& subject, int max_age) = 0;

    virtual std::pair<bool, std::string>
    get_subject(const std::string& issuer, const std::string& token) = 0;
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
                     const std::string& subject,
                     int max_age) override
    {
        auto now = std::chrono::system_clock::now();

        return ::jwt::create()
               .set_issuer(issuer)
               .set_audience(subject)
               .set_subject(subject)
               .set_issued_at(now)
               .set_expires_at(now + std::chrono::seconds {max_age})
               .sign(m_algo);
    }

    std::pair<bool, std::string>
    get_subject(const std::string& issuer, const std::string& token) override
    {
        bool rval = false;
        std::string subject;

        try
        {
            auto d = ::jwt::decode(token) ;

            ::jwt::verify()
            .allow_algorithm(m_algo)
            .with_issuer(issuer)
            .verify(d);

            subject = d.get_subject();
            rval = true;
        }
        catch (const std::exception& e)
        {
        }

        return {rval, subject};
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

mxs::JwtAlgo auto_detect_algorithm(const mxs::Config& cnf, const std::string& key)
{
    mxs::JwtAlgo algo = mxs::JwtAlgo::HS256;

    if (key.empty())
    {
        MXB_NOTICE("Using HS256 for JWT signatures");
        return algo;
    }

    BIO* b = BIO_new_mem_buf(key.data(), key.size());

    if (EVP_PKEY* pk = PEM_read_bio_PrivateKey(b, nullptr, nullptr, nullptr))
    {
        switch (EVP_PKEY_id(pk))
        {
        case EVP_PKEY_RSA:
        case EVP_PKEY_RSA2:
#ifdef OPENSSL_1_1
        case EVP_PKEY_RSA_PSS:
#endif
            MXB_NOTICE("Using PS256 for JWT signatures");
            algo = mxs::JwtAlgo::PS256;
            break;

        case EVP_PKEY_EC:
            if (EC_KEY* ec = EVP_PKEY_get1_EC_KEY(pk))
            {
                if (const EC_GROUP* grp = EC_KEY_get0_group(ec))
                {
                    int nid = EC_GROUP_get_curve_name(grp);

                    switch (nid)
                    {
                    case NID_X9_62_prime256v1:
                        MXB_NOTICE("Using ES256 for JWT signatures");
                        algo = mxs::JwtAlgo::ES256;
                        break;

                    case NID_secp384r1:
                        MXB_NOTICE("Using ES384 for JWT signatures");
                        algo = mxs::JwtAlgo::ES384;
                        break;

                    case NID_secp521r1:
                        MXB_NOTICE("Using ES512 for JWT signatures");
                        algo = mxs::JwtAlgo::ES512;
                        break;

                    default:
                        MXB_INFO("Cannot auto-detect EC curve, unknown NID: %d", nid);
                        break;
                    }
                }

                EC_KEY_free(ec);
            }
            break;

#ifdef OPENSSL_1_1
        case EVP_PKEY_ED25519:
            MXB_NOTICE("Using ED25519 for JWT signatures");
            algo = mxs::JwtAlgo::ED25519;
            break;

        case EVP_PKEY_ED448:
            MXB_NOTICE("Using ED448 for JWT signatures");
            algo = mxs::JwtAlgo::ED448;
            break;

#endif
        default:
            break;
        }
    }

    BIO_free(b);

    if (algo == mxs::JwtAlgo::HS256)
    {
        MXB_NOTICE("Could not auto-detect JWT signature algorithm, using HS256 for JWT signatures.");
    }

    return algo;
}

struct ThisUnit
{
    std::mutex               lock;
    std::unique_ptr<Jwt>     jwt;
    std::vector<std::string> extra_certs;
};

ThisUnit this_unit;

template<class Algo, class Decoded>
bool verify_with_alg(const std::string& issuer, Decoded& d)
{
    bool ok = false;

    for (const auto& cert : this_unit.extra_certs)
    {
        try
        {
            ::jwt::verify().allow_algorithm(Algo {cert}).verify(d);

            ok = true;
            break;
        }
        catch (const std::exception& e)
        {
            MXB_DEBUG("JWT verify failed: %s", e.what());
        }
    }

    return ok;
}

std::pair<bool, std::string> verify_extra(const std::string& issuer, const std::string& token)
{
    bool ok = false;
    std::string sub;

    try
    {
        auto d = ::jwt::decode(token);
        auto algo = d.get_algorithm();

        if (algo == "RS256")
        {
            ok = verify_with_alg<Sig::rs256>(issuer, d);
        }
        else if (algo == "RS384")
        {
            ok = verify_with_alg<Sig::rs384>(issuer, d);
        }
        else if (algo == "RS512")
        {
            ok = verify_with_alg<Sig::rs512>(issuer, d);
        }
        else if (algo == "ES256")
        {
            ok = verify_with_alg<Sig::es256>(issuer, d);
        }
        else if (algo == "ES384")
        {
            ok = verify_with_alg<Sig::es384>(issuer, d);
        }
        else if (algo == "ES512")
        {
            ok = verify_with_alg<Sig::es512>(issuer, d);
        }
        else if (algo == "PS256")
        {
            ok = verify_with_alg<Sig::ps256>(issuer, d);
        }
        else if (algo == "PS384")
        {
            ok = verify_with_alg<Sig::ps384>(issuer, d);
        }
        else if (algo == "PS512")
        {
            ok = verify_with_alg<Sig::ps512>(issuer, d);
        }
        else if (algo == "EdDSA")
        {
            if (d.has_header_claim("crv") && d.get_header_claim("crv").as_string() == "Ed448")
            {
                ok = verify_with_alg<Sig::ed448>(issuer, d);
            }
            else
            {
                // Fall back to Ed25519 if the "crv" claim is not present
                ok = verify_with_alg<Sig::ed25519>(issuer, d);
            }
        }

        if (ok)
        {
            sub = d.get_subject();
        }
    }
    catch (const std::exception& e)
    {
        MXB_INFO("Token verification failed: %s", e.what());
    }

    return {ok, sub};
}
}

namespace maxscale
{
namespace jwt
{

bool init()
{
    std::lock_guard guard(this_unit.lock);
    const auto& cnf = mxs::Config::get();
    std::unique_ptr<Jwt> jwt;
    std::string key;
    std::string cert;
    std::string err;

    if (!cnf.admin_ssl_key.empty())
    {
        if (std::tie(key, err) = mxb::load_file<std::string>(cnf.admin_ssl_key); !err.empty())
        {
            MXB_ERROR("Failed to load REST API private key: %s", err.c_str());
            return false;
        }

        if (std::tie(cert, err) = mxb::load_file<std::string>(cnf.admin_ssl_cert); !err.empty())
        {
            MXB_ERROR("Failed to load REST API public certificate: %s", err.c_str());
            return false;
        }
    }

    std::vector<std::string> extra_certs;

    for (auto path : cnf.admin_jwt_extra_certs)
    {
        if (auto [extra_cert, v_err] = mxb::load_file<std::string>(path); v_err.empty())
        {
            extra_certs.push_back(cert_cleanup(extra_cert));
        }
        else
        {
            MXB_ERROR("Failed to load JWT verification certificate: %s", err.c_str());
            return false;
        }
    }

    mxs::JwtAlgo algo = cnf.admin_jwt_algorithm;

    if (algo == mxs::JwtAlgo::AUTO)
    {
        algo = auto_detect_algorithm(cnf, key);
    }

    if (!is_pubkey_alg(algo) && !cnf.admin_jwt_key.empty())
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
        switch (algo)
        {
        case mxs::JwtAlgo::HS256:
            check_key(key, 256);
            jwt = make_jwt(Sig::hs256 {key.empty() ? rand_key(256) : key});
            break;

        case mxs::JwtAlgo::HS384:
            check_key(key, 384);
            jwt = make_jwt(Sig::hs384 {key.empty() ? rand_key(384) : key});
            break;

        case mxs::JwtAlgo::HS512:
            check_key(key, 512);
            jwt = make_jwt(Sig::hs512 {key.empty() ? rand_key(512) : key});
            break;

        case mxs::JwtAlgo::RS256:
            jwt = make_jwt(Sig::rs256 {cert, key});
            break;

        case mxs::JwtAlgo::RS384:
            jwt = make_jwt(Sig::rs384 {cert, key});
            break;

        case mxs::JwtAlgo::RS512:
            jwt = make_jwt(Sig::rs512 {cert, key});
            break;

        case mxs::JwtAlgo::ES256:
            jwt = make_jwt(Sig::es256 {cert, key});
            break;

        case mxs::JwtAlgo::ES384:
            jwt = make_jwt(Sig::es384 {cert, key});
            break;

        case mxs::JwtAlgo::ES512:
            jwt = make_jwt(Sig::es512 {cert, key});
            break;

        case mxs::JwtAlgo::PS256:
            jwt = make_jwt(Sig::ps256 {cert, key});
            break;

        case mxs::JwtAlgo::PS384:
            jwt = make_jwt(Sig::ps384 {cert, key});
            break;

        case mxs::JwtAlgo::PS512:
            jwt = make_jwt(Sig::ps512 {cert, key});
            break;

        case mxs::JwtAlgo::ED25519:
#ifdef OPENSSL_1_1
            jwt = make_jwt(Sig::ed25519 {cert, key});
#else
            MXB_ERROR("ED25519 is not supported on this system.");
#endif
            break;

        case mxs::JwtAlgo::ED448:
#ifdef OPENSSL_1_1
            jwt = make_jwt(Sig::ed448 {cert, key});
#else
            MXB_ERROR("ED448 is not supported on this system.");
#endif
            break;

        default:
            mxb_assert_message(!true, "We shouldn't end up here.");
            break;
        }

        if (jwt)
        {
            this_unit.jwt = std::move(jwt);
            this_unit.extra_certs = std::move(extra_certs);
        }
    }
    catch (const std::exception& e)
    {
        MXB_ERROR("Key initialization failed: %s", e.what());
    }

    return this_unit.jwt.get();
}

std::string create(const std::string& issuer, const std::string& subject, int max_age)
{
    std::lock_guard guard(this_unit.lock);
    return this_unit.jwt->sign(issuer, subject, max_age);
}

std::pair<bool, std::string> get_subject(const std::string& issuer, const std::string& token)
{
    std::lock_guard guard(this_unit.lock);
    auto [ok, sub] = this_unit.jwt->get_subject(issuer, token);

    if (!ok)
    {
        std::tie(ok, sub) = verify_extra(issuer, token);
    }

    return {ok, sub};
}
}
}
