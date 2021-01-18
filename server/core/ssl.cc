/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file ssl.c  -  SSL generic functions
 *
 * SSL is intended to be available in conjunction with a variety of protocols
 * on either the client or server side.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 02/02/16     Martin Brampton         Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <maxscale/dcb.hh>
#include <maxscale/poll.hh>
#include <maxscale/service.hh>
#include <maxscale/routingworker.hh>

static RSA* rsa_512 = NULL;
static RSA* rsa_1024 = NULL;

const MXS_ENUM_VALUE ssl_version_values[] =
{
    {"MAX",    SERVICE_SSL_TLS_MAX},
    {"TLSv10", SERVICE_TLS10      },
    {"TLSv11", SERVICE_TLS11      },
    {"TLSv12", SERVICE_TLS12      },
    {"TLSv13", SERVICE_TLS13      },
    {NULL}
};

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

/**
 * Returns an enum ssl_method_type value as string.
 *
 * @param method A method type.
 * @return The method type expressed as a string.
 */
const char* ssl_method_type_to_string(ssl_method_type_t method_type)
{
    switch (method_type)
    {
    case SERVICE_TLS10:
        return "TLSv10";

    case SERVICE_TLS11:
        return "TLSv11";

    case SERVICE_TLS12:
        return "TLSv12";

    case SERVICE_TLS13:
        return "TLSv13";

    case SERVICE_SSL_MAX:
    case SERVICE_TLS_MAX:
    case SERVICE_SSL_TLS_MAX:
        return "MAX";

    default:
        return "Unknown";
    }
}

ssl_method_type_t string_to_ssl_method_type(const char* str)
{
    if (strcasecmp("MAX", str) == 0)
    {
        return SERVICE_SSL_TLS_MAX;
    }
    else if (strcasecmp("TLSV10", str) == 0)
    {
        return SERVICE_TLS10;
    }
    else if (strcasecmp("TLSV11", str) == 0)
    {
        return SERVICE_TLS11;
    }
    else if (strcasecmp("TLSV12", str) == 0)
    {
        return SERVICE_TLS12;
    }
    else if (strcasecmp("TLSV13", str) == 0)
    {
        return SERVICE_TLS13;
    }
    return SERVICE_SSL_UNKNOWN;
}

// thread-local non-POD types are not supported with older versions of GCC
static thread_local std::string* ssl_errbuf;

static const char* get_ssl_errors()
{
    if (ssl_errbuf == NULL)
    {
        ssl_errbuf = new std::string;
    }

    char errbuf[200];   // Enough space according to OpenSSL documentation
    ssl_errbuf->clear();

    for (int err = ERR_get_error(); err; err = ERR_get_error())
    {
        if (!ssl_errbuf->empty())
        {
            ssl_errbuf->append(", ");
        }
        ssl_errbuf->append(ERR_error_string(err, errbuf));
    }

    return ssl_errbuf->c_str();
}

namespace maxscale
{

SSLConfig::SSLConfig(const MXS_CONFIG_PARAMETER& params)
    : key(params.get_string(CN_SSL_KEY))
    , cert(params.get_string(CN_SSL_CERT))
    , ca(params.get_string(CN_SSL_CA_CERT))
    , version((ssl_method_type_t)params.get_enum(CN_SSL_VERSION, ssl_version_values))
    , verify_depth(params.get_integer(CN_SSL_CERT_VERIFY_DEPTH))
    , verify_peer(params.get_bool(CN_SSL_VERIFY_PEER_CERTIFICATE))
    , cipher(params.get_string(CN_SSL_CIPHER))
{
}

// static
std::unique_ptr<SSLContext> SSLContext::create(const MXS_CONFIG_PARAMETER& params)
{
    mxb_assert(params.get_string(CN_SSL_CA_CERT).empty()
               || access(params.get_string(CN_SSL_CA_CERT).c_str(), F_OK) == 0);
    mxb_assert(params.get_string(CN_SSL_CERT).empty()
               || access(params.get_string(CN_SSL_CERT).c_str(), F_OK) == 0);
    mxb_assert(params.get_string(CN_SSL_KEY).empty()
               || access(params.get_string(CN_SSL_KEY).c_str(), F_OK) == 0);

    std::unique_ptr<SSLContext> ssl(new(std::nothrow) SSLContext(SSLConfig(params)));

    if (ssl && !ssl->init())
    {
        ssl.reset();
    }

    return ssl;
}

SSLContext::SSLContext(const SSLConfig& cfg)
    : m_cfg(cfg)
{
}

bool SSLContext::init()
{
    bool rval = true;

    switch (m_cfg.version)
    {
    case SERVICE_TLS10:
#ifndef OPENSSL_1_1
        m_method = (SSL_METHOD*)TLSv1_method();
#else
        MXS_ERROR("TLSv1.0 is not supported on this system.");
        return false;
#endif
        break;


    case SERVICE_TLS11:
#if defined (OPENSSL_1_0) || defined (OPENSSL_1_1)
        m_method = (SSL_METHOD*)TLSv1_1_method();
#else
        MXS_ERROR("TLSv1.1 is not supported on this system.");
        return false;
#endif
        break;

    case SERVICE_TLS12:
#if defined (OPENSSL_1_0) || defined (OPENSSL_1_1)
        m_method = (SSL_METHOD*)TLSv1_2_method();
#else
        MXS_ERROR("TLSv1.2 is not supported on this system.");
        return false;
#endif
        break;

    case SERVICE_TLS13:
#ifdef OPENSSL_1_1
        m_method = (SSL_METHOD*)TLS_method();
#else
        MXS_ERROR("TLSv1.3 is not supported on this system.");
        return false;
#endif
        break;

    /** Rest of these use the maximum available SSL/TLS methods */
    case SERVICE_SSL_MAX:
        m_method = (SSL_METHOD*)SSLv23_method();
        break;

    case SERVICE_TLS_MAX:
        m_method = (SSL_METHOD*)SSLv23_method();
        break;

    case SERVICE_SSL_TLS_MAX:
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

    if (m_cfg.version == SERVICE_TLS13)
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

SSLProvider::SSLProvider(std::unique_ptr<mxs::SSLContext> context)
    : m_context {std::move(context)}
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
       << "\tSSL method type:                     " << ssl_method_type_to_string(version) << "\n"
       << "\tSSL certificate verification depth:  " << verify_depth << "\n"
       << "\tSSL peer verification :              " << (verify_peer ? "true" : "false") << "\n"
       << "\tSSL certificate:                     " << cert << "\n"
       << "\tSSL key:                             " << key << "\n"
       << "\tSSL CA certificate:                  " << ca << "\n";

    return ss.str();
}
}
