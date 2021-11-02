/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>
#include <maxscale/ssl.hh>
#include <maxscale/routingworker.hh>

const MXS_ENUM_VALUE ssl_version_values[] =
{
    {"MAX",    mxb::ssl_version::SSL_TLS_MAX},
    {"TLSv10", mxb::ssl_version::TLS10      },
    {"TLSv11", mxb::ssl_version::TLS11      },
    {"TLSv12", mxb::ssl_version::TLS12      },
    {"TLSv13", mxb::ssl_version::TLS13      },
    {NULL}
};

namespace
{

static RSA* rsa_512 = NULL;
static RSA* rsa_1024 = NULL;

static RSA* create_rsa(int bits)
{
#ifdef OPENSSL_1_1
    BIGNUM* bn = BN_new();
    BN_set_word(bn, RSA_F4);
    RSA* rsa = RSA_new();
    RSA_generate_key_ex(rsa, bits, bn, NULL);
    BN_free(bn);
    return rsa;
#else
    return RSA_generate_key(bits, RSA_F4, NULL, NULL);
#endif
}

/**
 * The RSA key generation callback function for OpenSSL.
 * @param s SSL structure
 * @param is_export Not used
 * @param keylength Length of the key
 * @return Pointer to RSA structure
 */
static RSA* tmp_rsa_callback(SSL* s, int is_export, int keylength)
{
    RSA* rsa_tmp = NULL;

    switch (keylength)
    {
    case 512:
        if (rsa_512)
        {
            rsa_tmp = rsa_512;
        }
        else
        {
            /* generate on the fly, should not happen in this example */
            rsa_tmp = create_rsa(keylength);
            rsa_512 = rsa_tmp;      /* Remember for later reuse */
        }
        break;

    case 1024:
        if (rsa_1024)
        {
            rsa_tmp = rsa_1024;
        }
        break;

    default:
        /* Generating a key on the fly is very costly, so use what is there */
        if (rsa_1024)
        {
            rsa_tmp = rsa_1024;
        }
        else
        {
            rsa_tmp = rsa_512;      /* Use at least a shorter key */
        }
    }
    return rsa_tmp;
}

static thread_local std::string ssl_errbuf;

static const char* get_ssl_errors()
{
    char errbuf[200];   // Enough space according to OpenSSL documentation
    ssl_errbuf.clear();

    for (int err = ERR_get_error(); err; err = ERR_get_error())
    {
        if (!ssl_errbuf.empty())
        {
            ssl_errbuf.append(", ");
        }
        ssl_errbuf.append(ERR_error_string(err, errbuf));
    }

    return ssl_errbuf.c_str();
}
}

namespace maxscale
{

SSLConfig::SSLConfig(const mxs::ConfigParameters& params)
    : mxb::SSLConfig(params.get_string(CN_SSL_KEY), params.get_string(CN_SSL_CERT),
                     params.get_string(CN_SSL_CA_CERT))
{
    if (params.contains(CN_SSL_CRL))
    {
        crl = params.get_string(CN_SSL_CRL);
    }
    if (params.contains(CN_SSL_VERSION))
    {
        version = (mxb::ssl_version::Version)params.get_enum(CN_SSL_VERSION, ssl_version_values);
    }
    if (params.contains(CN_SSL_CERT_VERIFY_DEPTH))
    {
        verify_depth = params.get_integer(CN_SSL_CERT_VERIFY_DEPTH);
    }
    if (params.contains(CN_SSL_VERIFY_PEER_CERTIFICATE))
    {
        verify_peer = params.get_bool(CN_SSL_VERIFY_PEER_CERTIFICATE);
    }
    if (params.contains(CN_SSL_VERIFY_PEER_HOST))
    {
        verify_host = params.get_bool(CN_SSL_VERIFY_PEER_HOST);
    }
    if (params.contains(CN_SSL_CIPHER))
    {
        cipher = params.get_string(CN_SSL_CIPHER);
    }
}

// static
std::unique_ptr<SSLContext> SSLContext::create(const mxs::ConfigParameters& params)
{
    std::unique_ptr<SSLContext> rval(new(std::nothrow) SSLContext());
    if (rval)
    {
        if (!rval->configure(params))
        {
            rval = nullptr;
        }
    }
    return rval;
}

bool SSLContext::init()
{
    switch (m_cfg.version)
    {
    case mxb::ssl_version::TLS10:
#ifndef OPENSSL_1_1
        m_method = (SSL_METHOD*)TLSv1_method();
#else
        MXS_ERROR("TLSv1.0 is not supported on this system.");
        return false;
#endif
        break;


    case mxb::ssl_version::TLS11:
#if defined (OPENSSL_1_0) || defined (OPENSSL_1_1)
        m_method = (SSL_METHOD*)TLSv1_1_method();
#else
        MXS_ERROR("TLSv1.1 is not supported on this system.");
        return false;
#endif
        break;

    case mxb::ssl_version::TLS12:
#if defined (OPENSSL_1_0) || defined (OPENSSL_1_1)
        m_method = (SSL_METHOD*)TLSv1_2_method();
#else
        MXS_ERROR("TLSv1.2 is not supported on this system.");
        return false;
#endif
        break;

    case mxb::ssl_version::TLS13:
#ifdef OPENSSL_1_1
        m_method = (SSL_METHOD*)TLS_method();
#else
        MXS_ERROR("TLSv1.3 is not supported on this system.");
        return false;
#endif
        break;

    /** Rest of these use the maximum available SSL/TLS methods */
    case mxb::ssl_version::SSL_MAX:
    case mxb::ssl_version::TLS_MAX:
    case mxb::ssl_version::SSL_TLS_MAX:
        m_method = (SSL_METHOD*)SSLv23_method();
        break;

    default:
        m_method = (SSL_METHOD*)SSLv23_method();
        break;
    }

    m_ctx = SSL_CTX_new(m_method);

    if (m_ctx == NULL)
    {
        MXS_ERROR("SSL context initialization failed: %s", get_ssl_errors());
        return false;
    }

    SSL_CTX_set_default_read_ahead(m_ctx, 0);

    /** Enable all OpenSSL bug fixes */
    SSL_CTX_set_options(m_ctx, SSL_OP_ALL);

    /** Disable SSLv3 */
    SSL_CTX_set_options(m_ctx, SSL_OP_NO_SSLv3);

    if (m_cfg.version == mxb::ssl_version::TLS13)
    {
        // There is no TLSv1_3_method function as the TLSv1_X_method functions are deprecated in favor of
        // disabling them via options.
        SSL_CTX_set_options(m_ctx, SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2);
    }

    // Disable session cache
    SSL_CTX_set_session_cache_mode(m_ctx, SSL_SESS_CACHE_OFF);

    //
    // Note: This is not safe if SSL initialization is done concurrently
    //
    /** Generate the 512-bit and 1024-bit RSA keys */
    if (rsa_512 == NULL && (rsa_512 = create_rsa(512)) == NULL)
    {
        MXS_ERROR("512-bit RSA key generation failed.");
        return false;
    }
    else if (rsa_1024 == NULL && (rsa_1024 = create_rsa(1024)) == NULL)
    {
        MXS_ERROR("1024-bit RSA key generation failed.");
        return false;
    }
    else
    {
        mxb_assert(rsa_512 && rsa_1024);
        SSL_CTX_set_tmp_rsa_callback(m_ctx, tmp_rsa_callback);
    }

    if (!m_cfg.ca.empty())
    {
        /* Load the CA certificate into the SSL_CTX structure */
        if (!SSL_CTX_load_verify_locations(m_ctx, m_cfg.ca.c_str(), NULL))
        {
            MXS_ERROR("Failed to set Certificate Authority file: %s", get_ssl_errors());
            return false;
        }
    }
    else if (SSL_CTX_set_default_verify_paths(m_ctx) == 0)
    {
        MXS_ERROR("Failed to set default CA verify paths: %s", get_ssl_errors());
        return false;
    }

    if (!m_cfg.crl.empty())
    {
        X509_STORE* store = SSL_CTX_get_cert_store(m_ctx);

        if (FILE* fp = fopen(m_cfg.crl.c_str(), "rb"))
        {
            X509_CRL* crl = nullptr;

            if (!PEM_read_X509_CRL(fp, &crl, nullptr, nullptr))
            {
                MXS_ERROR("Failed to process CRL file: %s", get_ssl_errors());
                fclose(fp);
                return false;
            }
            else if (!X509_STORE_add_crl(store, crl))
            {
                MXS_ERROR("Failed to set CRL: %s", get_ssl_errors());
                fclose(fp);
                return false;
            }
            else
            {
                X509_VERIFY_PARAM* param = X509_VERIFY_PARAM_new();
                X509_VERIFY_PARAM_set_flags(param, X509_V_FLAG_CRL_CHECK);
                SSL_CTX_set1_param(m_ctx, param);
                X509_VERIFY_PARAM_free(param);
            }
        }
        else
        {
            MXS_ERROR("Failed to load CRL file: %d, %s", errno, mxs_strerror(errno));
            return false;
        }
    }

    if (!m_cfg.cert.empty() && !m_cfg.key.empty())
    {
        /** Load the server certificate */
        if (SSL_CTX_use_certificate_chain_file(m_ctx, m_cfg.cert.c_str()) <= 0)
        {
            MXS_ERROR("Failed to set server SSL certificate: %s", get_ssl_errors());
            return false;
        }

        /* Load the private-key corresponding to the server certificate */
        if (SSL_CTX_use_PrivateKey_file(m_ctx, m_cfg.key.c_str(), SSL_FILETYPE_PEM) <= 0)
        {
            MXS_ERROR("Failed to set server SSL key: %s", get_ssl_errors());
            return false;
        }

        /* Check if the server certificate and private-key matches */
        if (!SSL_CTX_check_private_key(m_ctx))
        {
            MXS_ERROR("Server SSL certificate and key do not match: %s", get_ssl_errors());
            return false;
        }
    }

    /* Set to require peer (client) certificate verification */
    if (m_cfg.verify_peer)
    {
        SSL_CTX_set_verify(m_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    }

    /* Set the verification depth */
    SSL_CTX_set_verify_depth(m_ctx, m_cfg.verify_depth);

    if (!m_cfg.cipher.empty())
    {
        if (SSL_CTX_set_cipher_list(m_ctx, m_cfg.cipher.c_str()) == 0)
        {
            MXS_ERROR("Could not set cipher list '%s': %s", m_cfg.cipher.c_str(), get_ssl_errors());
            return false;
        }
    }

    return true;
}

SSLContext::~SSLContext()
{
    SSL_CTX_free(m_ctx);
}

SSLContext::SSLContext(SSLContext&& rhs) noexcept
    : m_ctx(rhs.m_ctx)
    , m_method(rhs.m_method)
    , m_cfg(std::move(rhs.m_cfg))
{
    rhs.m_method = nullptr;
    rhs.m_ctx = nullptr;
}

SSLContext& SSLContext::operator=(SSLContext&& rhs) noexcept
{
    reset();
    m_cfg = std::move(rhs.m_cfg);
    std::swap(m_method, rhs.m_method);
    std::swap(m_ctx, rhs.m_ctx);
    return *this;
}

SSLProvider::SSLProvider(std::unique_ptr<mxs::SSLContext> context)
    : m_context{std::move(context)}
{
}

mxs::SSLContext* SSLProvider::context() const
{
    mxb_assert_message(mxs::RoutingWorker::get_current(), "Must be used on a RoutingWorker");
    return m_context.get();
}

const mxs::SSLConfig* SSLProvider::config() const
{
    return m_context ? &(m_context->config()) : nullptr;
}

void SSLProvider::set_context(std::unique_ptr<mxs::SSLContext> ssl)
{
    mxb_assert(ssl);
    m_context = std::move(ssl);
}

std::string SSLConfig::to_string() const
{
    std::ostringstream ss;

    ss << "\tSSL initialized:                     yes\n"
       << "\tSSL method type:                     " << mxb::ssl_version::to_string(version) << "\n"
       << "\tSSL certificate verification depth:  " << verify_depth << "\n"
       << "\tSSL peer verification :              " << (verify_peer ? "true" : "false") << "\n"
       << "\tSSL peer host verification :         " << (verify_host ? "true" : "false") << "\n"
       << "\tSSL certificate:                     " << cert << "\n"
       << "\tSSL key:                             " << key << "\n"
       << "\tSSL CA certificate:                  " << ca << "\n";

    return ss.str();
}

bool SSLContext::read_configuration(const std::string& name, const mxs::ConfigParameters& params,
                                    bool require_cert)
{
    bool ok = true;
    // The enum values convert to bool
    int value = params.get_enum(CN_SSL, ssl_setting_values());
    mxb_assert(value != -1);
    if (value)
    {
        auto namez = name.c_str();
        if (require_cert)
        {
            if (!params.contains(CN_SSL_CERT))
            {
                MXS_ERROR("Server certificate missing for listener '%s'."
                          "Please provide the path to the server certificate by adding "
                          "the ssl_cert=<path> parameter",
                          namez);
                ok = false;
            }

            if (!params.contains(CN_SSL_KEY))
            {
                MXS_ERROR("Server private key missing for listener '%s'. "
                          "Please provide the path to the server certificate key by "
                          "adding the ssl_key=<path> parameter",
                          namez);
                ok = false;
            }
        }

        if (ok)
        {
            ok = configure(params);
        }
    }
    else
    {
        // OK, no SSL configured. Reset to empty.
        reset();
    }
    return ok;
}

void SSLContext::reset()
{
    m_cfg = SSLConfig();
    m_method = nullptr;
    SSL_CTX_free(m_ctx);
    m_ctx = nullptr;
}

bool SSLContext::configure(const mxs::ConfigParameters& params)
{
    reset();
    mxb_assert(params.get_string(CN_SSL_CA_CERT).empty()
               || access(params.get_string(CN_SSL_CA_CERT).c_str(), F_OK) == 0);
    mxb_assert(params.get_string(CN_SSL_CERT).empty()
               || access(params.get_string(CN_SSL_CERT).c_str(), F_OK) == 0);
    mxb_assert(params.get_string(CN_SSL_KEY).empty()
               || access(params.get_string(CN_SSL_KEY).c_str(), F_OK) == 0);

    m_cfg = SSLConfig(params);

#ifndef OPENSSL_1_1
    if (m_cfg.verify_host)
    {
        MXS_ERROR("%s is not supported on this system.", CN_SSL_VERIFY_PEER_HOST);
        return false;
    }
#endif

    return init();
}
}

const MXS_ENUM_VALUE* ssl_setting_values()
{
    // Values for the `ssl` parameter. These are plain boolean types but for legacy
    // reasons the required and disabled keywords need to be allowed.
    static const MXS_ENUM_VALUE ssl_values[] =
    {
        {"required", 1},
        {"true",     1},
        {"yes",      1},
        {"on",       1},
        {"1",        1},
        {"disabled", 0},
        {"false",    0},
        {"no",       0},
        {"off",      0},
        {"0",        0},
        {NULL}
    };
    return ssl_values;
}
