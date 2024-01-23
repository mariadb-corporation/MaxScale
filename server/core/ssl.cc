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

#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>
#include <maxscale/ssl.hh>
#include <maxscale/routingworker.hh>

// Disable all OpenSSL deprecation warnings
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#ifdef OPENSSL_1_1
#include <openssl/x509v3.h>
#endif
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <unistd.h>

namespace
{

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

// static
std::unique_ptr<SSLContext> SSLContext::create(const mxb::SSLConfig& config, mxb::KeyUsage usage)
{
    auto rval = std::make_unique<SSLContext>(usage);
    if (!rval->configure(config))
    {
        rval = nullptr;
    }
    return rval;
}

bool SSLContext::init()
{
    // Always use the general-purpose version-flexible TLS method, then disable old versions according to
    // configuration.
    auto method = (SSL_METHOD*)SSLv23_method();

    m_ctx = SSL_CTX_new(method);
    if (m_ctx == NULL)
    {
        MXB_ERROR("SSL context initialization failed: %s", get_ssl_errors());
        return false;
    }

    SSL_CTX_set_default_read_ahead(m_ctx, 0);

    /** Enable all OpenSSL bug fixes */
    SSL_CTX_set_options(m_ctx, SSL_OP_ALL);

    /** Disable SSLv3 always, SSLv2 should be disabled by default. */
    SSL_CTX_set_options(m_ctx, SSL_OP_NO_SSLv3);

    switch (m_cfg.version)
    {
    case mxb::ssl_version::TLS10:
    case mxb::ssl_version::SSL_TLS_MAX:
    default:
        // Disable nothing, allow 1.0, 1.1, 1.2, 1.3. In practice, recent OpenSSL-versions may not support
        // some old protocols. OpenSSL selects the best available SSL/TLS method both ends support.
        break;

    case mxb::ssl_version::TLS11:
        // Allow 1.1, 1.2, 1.3 (in OpenSSL 1.1)
        SSL_CTX_set_options(m_ctx, SSL_OP_NO_TLSv1);
        break;

    case mxb::ssl_version::TLS12:
        // Allow 1.2, 1.3 (in OpenSSL 1.1)
        SSL_CTX_set_options(m_ctx, SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
        break;

    case mxb::ssl_version::TLS13:
        // Allow 1.3 (in OpenSSL 1.1)
#ifdef OPENSSL_1_1
        SSL_CTX_set_options(m_ctx, SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2);
#else
        MXB_ERROR("TLSv1.3 is not supported on this system.");
        return false;
#endif
        break;
    }

    // Disable session cache
    SSL_CTX_set_session_cache_mode(m_ctx, SSL_SESS_CACHE_OFF);

    SSL_CTX_set_ecdh_auto(m_ctx, 1);

    if (!m_cfg.ca.empty())
    {
        /* Load the CA certificate into the SSL_CTX structure */
        if (!SSL_CTX_load_verify_locations(m_ctx, m_cfg.ca.c_str(), NULL))
        {
            MXB_ERROR("Failed to set Certificate Authority file: %s", get_ssl_errors());
            return false;
        }
    }
    else if (SSL_CTX_set_default_verify_paths(m_ctx) == 0)
    {
        MXB_ERROR("Failed to set default CA verify paths: %s", get_ssl_errors());
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
                MXB_ERROR("Failed to process CRL file: %s", get_ssl_errors());
                fclose(fp);
                return false;
            }
            else if (!X509_STORE_add_crl(store, crl))
            {
                MXB_ERROR("Failed to set CRL: %s", get_ssl_errors());
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
            MXB_ERROR("Failed to load CRL file: %d, %s", errno, mxb_strerror(errno));
            return false;
        }
    }

    if (!m_cfg.cert.empty() && !m_cfg.key.empty())
    {
        /** Load the server certificate */
        if (SSL_CTX_use_certificate_chain_file(m_ctx, m_cfg.cert.c_str()) <= 0)
        {
            MXB_ERROR("Failed to set server SSL certificate: %s", get_ssl_errors());
            return false;
        }

        /* Load the private-key corresponding to the server certificate */
        if (SSL_CTX_use_PrivateKey_file(m_ctx, m_cfg.key.c_str(), SSL_FILETYPE_PEM) <= 0)
        {
            MXB_ERROR("Failed to set server SSL key: %s", get_ssl_errors());
            return false;
        }

        /* Check if the server certificate and private-key matches */
        if (!SSL_CTX_check_private_key(m_ctx))
        {
            MXB_ERROR("Server SSL certificate and key do not match: %s", get_ssl_errors());
            return false;
        }

#ifdef OPENSSL_1_1
        X509* cert = SSL_CTX_get0_certificate(m_ctx);
        uint32_t usage = X509_get_extended_key_usage(cert);

        // OpenSSL explicitly states that it returns UINT32_MAX if it doesn't have the extended key usage.
        if (usage != UINT32_MAX)
        {
            bool is_client = (usage & (XKU_SSL_SERVER | XKU_SSL_CLIENT)) == XKU_SSL_CLIENT;
            bool is_server = (usage & (XKU_SSL_SERVER | XKU_SSL_CLIENT)) == XKU_SSL_SERVER;

            if (!is_client && is_server && m_usage == mxb::KeyUsage::CLIENT)
            {
                MXB_ERROR("Certificate has serverAuth extended key usage when clientAuth was expected.");
                return false;
            }
            else if (!is_server && is_client && m_usage == mxb::KeyUsage::SERVER)
            {
                MXB_ERROR("Certificate has clientAuth extended key usage when serverAuth was expected.");
                return false;
            }
        }
#endif
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
            MXB_ERROR("Could not set cipher list '%s': %s", m_cfg.cipher.c_str(), get_ssl_errors());
            return false;
        }
    }

    return true;
}

SSLContext::SSLContext(mxb::KeyUsage usage)
    : m_usage(usage)
{
}

SSLContext::~SSLContext()
{
    SSL_CTX_free(m_ctx);
}

SSLContext::SSLContext(SSLContext&& rhs) noexcept
    : m_ctx(rhs.m_ctx)
    , m_cfg(std::move(rhs.m_cfg))
    , m_usage(rhs.m_usage)
{
    rhs.m_ctx = nullptr;
}

SSLContext& SSLContext::operator=(SSLContext&& rhs) noexcept
{
    if (this != &rhs)
    {
        SSL_CTX_free(m_ctx);
        m_ctx = rhs.m_ctx;
        rhs.m_ctx = nullptr;
        m_cfg = std::move(rhs.m_cfg);
        m_usage = rhs.m_usage;
    }
    return *this;
}

bool SSLContext::configure(const mxb::SSLConfig& config)
{
    mxb_assert(!m_ctx);     // Reconfiguration not allowed, new object must be created.
    mxb_assert(config.ca.empty() || access(config.ca.c_str(), F_OK) == 0);
    mxb_assert(config.cert.empty() || access(config.cert.c_str(), F_OK) == 0);
    mxb_assert(config.key.empty() || access(config.key.c_str(), F_OK) == 0);

    m_cfg = config;

#ifndef OPENSSL_1_1
    if (m_cfg.verify_host)
    {
        MXB_ERROR("%s is not supported on this system.", CN_SSL_VERIFY_PEER_HOST);
        return false;
    }
#endif

    return m_cfg.enabled ? init() : true;
}

SSL* SSLContext::open() const
{
    return SSL_new(m_ctx);
}
}
