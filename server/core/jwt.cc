/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/jwt.hh"

// Disable all OpenSSL deprecation warnings
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <jwt-cpp/jwt.h>
#include <random>
#include <openssl/rand.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#include <maxbase/assert.hh>
#include <maxbase/http.hh>
#include <maxbase/filesystem.hh>
#include <maxscale/config.hh>
#include <maxscale/utils.hh>

namespace
{

// Uncrustify doesn't like a leading :: when used with a template parameter and breaks unless there's a space
// between the < and the ::. A custom namespace works around this problem and it also makes it slightly more
// readable.
namespace Sig = ::jwt::algorithm;

struct Jwt;

struct ThisUnit
{
    std::mutex           lock;
    std::unique_ptr<Jwt> jwt;

    std::unordered_map<std::string, std::unique_ptr<Jwt>> extra_certs;
    std::string                                           extra_issuer;
};

ThisUnit this_unit;

std::string rand_key(int bits)
{
    std::string key;
    key.resize(bits / 8);

    if (RAND_bytes((uint8_t*)key.data(), key.size()) != 1)
    {
        throw std::runtime_error("Failed to generate random key.");
    }

    return key;
}

// See example implementation in: https://www.rfc-editor.org/rfc/rfc7515.txt
std::vector<uint8_t> from_base64url(std::string value)
{
    for (auto& c : value)
    {
        if (c == '-')
        {
            c = '+';
        }
        else if (c == '_')
        {
            c = '/';
        }
    }

    switch (value.size() % 4)
    {
    case 0:
        break;

    case 2:
        value += "==";
        break;

    case 3:
        value += "=";
        break;

    default:
        // Malformed input
        return {};
    }

    return mxs::from_base64(value);
}

std::string rsa_jwk_to_pem(std::string modulus, std::string exponent)
{
    auto mod = from_base64url(modulus);
    auto exp = from_base64url(exponent);

    if (mod.empty() || exp.empty())
    {
        return {};
    }

    RSA* rsa = RSA_new();
    BIGNUM* mod_bn = BN_bin2bn(mod.data(), mod.size(), nullptr);
    BIGNUM* exp_bn = BN_bin2bn(exp.data(), exp.size(), nullptr);
#ifdef OPENSSL_1_1
    RSA_set0_key(rsa, mod_bn, exp_bn, nullptr);
#else
    rsa->n = mod_bn;
    rsa->e = exp_bn;
#endif
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_RSA_PUBKEY(bio, rsa);

    char* ptr = nullptr;
    long len = BIO_get_mem_data(bio, &ptr);
    std::string cert(ptr, len);

    BIO_free(bio);
    RSA_free(rsa);

    return cert;
}

std::string ec_jwk_to_pem(std::string curve, std::string x_coord, std::string y_coord)
{
    auto x = from_base64url(x_coord);
    auto y = from_base64url(y_coord);

    if (x.empty() || y.empty())
    {
        return {};
    }

    EC_KEY* ec = EC_KEY_new();
    EC_GROUP* group = nullptr;

    if (curve == "P-256")
    {
        group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    }
    else if (curve == "P-384")
    {
        group = EC_GROUP_new_by_curve_name(NID_secp384r1);
    }
    else if (curve == "P-512")
    {
        group = EC_GROUP_new_by_curve_name(NID_secp521r1);
    }
    else
    {
        return {};
    }

    EC_KEY_set_group(ec, group);
    EC_KEY_set_public_key_affine_coordinates(ec,
                                             BN_bin2bn(x.data(), x.size(), nullptr),
                                             BN_bin2bn(y.data(), y.size(), nullptr));


    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_EC_PUBKEY(bio, ec);

    char* ptr = nullptr;
    long len = BIO_get_mem_data(bio, &ptr);
    std::string cert(ptr, len);

    EC_GROUP_free(group);
    EC_KEY_free(ec);

    return cert;
}

// Simple wrapper for the jwt-cpp types. Since the library uses template types and has a similar-ish
// namespace, this class hides it from the rest of MaxScale to avoid any problems.
template<class LibraryJwt>
class RealImp : public mxs::jwt::Claims::Imp
{
public:
    RealImp(LibraryJwt&& jwt)
        : m_jwt(std::move(jwt))
    {
    }

    std::optional<std::string> get(const std::string& name) override final
    {
        if (m_jwt.has_payload_claim(name))
        {
            return value_to_str(m_jwt.get_payload_claim(name));
        }
        else if (m_jwt.has_header_claim(name))
        {
            return value_to_str(m_jwt.get_header_claim(name));
        }

        return {};
    }

private:
    template<class Value>
    std::string value_to_str(Value v)
    {
        using type = ::jwt::json::type;
        std::ostringstream ss;

        switch (v.get_type())
        {
        case type::string:
            // The string types are surrounded by double quotes if streamed into a ostream. We
            // want the values without them since we'll be using them for string comparisons.
            ss << v.as_string();
            break;

        case type::boolean:
            // Format booleans in the same way as JSON.
            ss << (v.as_bool() ? "true" : "false");
            break;

        default:
            mxb_assert(!true);
            // fallthrough

        case type::integer:
        case type::number:
        case type::array:
        case type::object:
            // Everything else gets formatted using the type's built-in formatter. This works as
            // expected for integers and numbers and is also adequate for objects and arrays.
            ss << v;
            break;
        }

        return ss.str();
    }

    LibraryJwt m_jwt;
};

// Abstract base class for signature creation and verification
struct Jwt
{
    virtual ~Jwt() = default;

    virtual std::string sign(const std::string& issuer, const std::string& subject, int max_age,
                             std::map<std::string, std::string>) = 0;

    virtual std::optional<mxs::jwt::Claims>
    get_claims(const std::string& issuer, const std::string& token) = 0;
};

template<class Algorithm>
class JwtBase : public Jwt
{
public:
    JwtBase(Algorithm algo)
        : m_algo(std::move(algo))
    {
    }

    std::string sign(const std::string& issuer, const std::string& subject, int max_age,
                     std::map<std::string, std::string> claims) override final
    {
        auto now = std::chrono::system_clock::now();

        auto tok = ::jwt::create()
            .set_issuer(issuer)
            .set_audience(subject)
            .set_subject(subject)
            .set_issued_at(now)
            .set_expires_at(now + std::chrono::seconds {max_age});

        for (auto [k, v] : claims)
        {
            tok.set_payload_claim(k, ::jwt::claim(v));
        }

        return tok.sign(m_algo);
    }

    std::optional<mxs::jwt::Claims>
    get_claims(const std::string& issuer, const std::string& token) override
    {
        try
        {
            auto d = ::jwt::decode(token) ;

            ::jwt::verify()
            .allow_algorithm(m_algo)
            .with_issuer(issuer)
            .verify(d);

            return mxs::jwt::Claims(std::unique_ptr<mxs::jwt::Claims::Imp>(new RealImp(std::move(d))));
        }
        catch (const std::exception& e)
        {
            MXB_DEBUG("%s: %s", __func__, e.what());
        }

        return {};
    }

private:
    Algorithm m_algo;
};

template<class Algorithm>
std::unique_ptr<Jwt> make_jwt(Algorithm algo)
{
    return std::make_unique<JwtBase<Algorithm>>(std::move(algo));
}

std::pair<bool, std::unordered_map<std::string, std::unique_ptr<Jwt>>> get_oidc_certs(const std::string& url)
{
    std::unordered_map<std::string, std::unique_ptr<Jwt>> certs;
    bool ok = false;

    try
    {
        // See: https://openid.net/specs/openid-connect-discovery-1_0.html#ProviderConfig
        auto response = mxb::http::get(url + "/.well-known/openid-configuration");
        mxb::Json js;

        if (response.is_success() && js.load_string(response.body))
        {
            // Store the issuer field from the OIDC metadata. This should be the literal value stored in the
            // "iss" field of the JWTs. If it isn't then we're dealing with a broken provider.
            this_unit.extra_issuer = js.get_string("issuer");

            auto jwks_uri = js.get_string("jwks_uri");
            response = mxb::http::get(jwks_uri);

            if (response.is_success() && js.load_string(response.body))
            {
                for (auto k : ::jwt::parse_jwks(response.body))
                {
                    std::string type = k.get_key_type();
                    std::string algo = k.get_algorithm();
                    std::string kid = k.get_key_id();
                    std::string cert;
                    std::unique_ptr<Jwt> jwt;

                    if (type == "RSA")
                    {
                        cert = rsa_jwk_to_pem(k.get_jwk_claim("n").as_string(),
                                              k.get_jwk_claim("e").as_string());
                    }
                    else if (type == "EC")
                    {
                        cert = ec_jwk_to_pem(k.get_curve(),
                                             k.get_jwk_claim("x").as_string(),
                                             k.get_jwk_claim("y").as_string());
                    }

                    if (cert.empty())
                    {
                        MXB_ERROR("Failed to decode JWK '%s'", kid.c_str());
                    }
                    else if (algo == "RS256")
                    {
                        jwt = make_jwt(Sig::rs256 {cert});
                    }
                    else if (algo == "RS384")
                    {
                        jwt = make_jwt(Sig::rs384 {cert});
                    }
                    else if (algo == "RS512")
                    {
                        jwt = make_jwt(Sig::rs512 {cert});
                    }
                    else if (algo == "ES256")
                    {
                        jwt = make_jwt(Sig::es256 {cert});
                    }
                    else if (algo == "ES384")
                    {
                        jwt = make_jwt(Sig::es384 {cert});
                    }
                    else if (algo == "ES512")
                    {
                        jwt = make_jwt(Sig::es512 {cert});
                    }
                    else if (algo == "PS256")
                    {
                        jwt = make_jwt(Sig::ps256 {cert});
                    }
                    else if (algo == "PS384")
                    {
                        jwt = make_jwt(Sig::ps384 {cert});
                    }
                    else if (algo == "PS512")
                    {
                        jwt = make_jwt(Sig::ps512 {cert});
                    }
                    else
                    {
                        MXB_WARNING("JWK '%s' contains an unknown \"alg\" value: %s",
                                    kid.c_str(),
                                    algo.c_str());
                    }

                    if (jwt)
                    {
                        certs.emplace(kid, std::move(jwt));
                    }
                }

                ok = true;
            }
            else
            {
                MXB_ERROR("Request to '%s' failed: %d, %s",
                          jwks_uri.c_str(), response.code, response.body.c_str());
            }
        }
        else
        {
            MXB_ERROR("Request to '%s' failed: %d, %s",
                      url.c_str(), response.code, response.body.c_str());
        }
    }
    catch (const std::exception& e)
    {
        MXB_ERROR("%s", e.what());
    }

    return {ok, std::move(certs)};
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

        EVP_PKEY_free(pk);
    }

    BIO_free(b);

    if (algo == mxs::JwtAlgo::HS256)
    {
        MXB_NOTICE("Could not auto-detect JWT signature algorithm, using HS256 for JWT signatures.");
    }

    return algo;
}

std::optional<mxs::jwt::Claims> verify_extra(const std::string& issuer, const std::string& token)
{
    try
    {
        auto d = ::jwt::decode(token);

        if (auto it = this_unit.extra_certs.find(d.get_key_id()); it != this_unit.extra_certs.end())
        {
            return it->second->get_claims(issuer, token);
        }
    }
    catch (const std::exception& e)
    {
        MXB_INFO("Token verification failed: %s", e.what());
    }

    return {};
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

    std::unordered_map<std::string, std::unique_ptr<Jwt>> extra_certs;

    if (const auto& url = cnf.admin_oidc_url; !url.empty())
    {
        auto [ok, certs] = get_oidc_certs(url);

        if (ok)
        {
            extra_certs = std::move(certs);
        }
        else
        {
            MXB_ERROR("Failed to load JWK set from '%s'", url.c_str());
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

std::string create(const std::string& issuer, const std::string& subject, int max_age,
                   std::map<std::string, std::string> claims)
{
    std::lock_guard guard(this_unit.lock);
    return this_unit.jwt->sign(issuer, subject, max_age, std::move(claims));
}

std::optional<Claims> decode(const std::string& issuer, const std::string& token)
{
    std::lock_guard guard(this_unit.lock);
    auto claims = this_unit.jwt->get_claims(issuer, token);

    if (!claims && !this_unit.extra_certs.empty())
    {
        claims = verify_extra(this_unit.extra_issuer, token);
    }

    return claims;
}
}
}
