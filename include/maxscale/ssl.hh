/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

/**
 * @file ssl.hh
 *
 * The SSL definitions for MaxScale
 */

#include <maxscale/ccdefs.hh>

#include <maxscale/protocol.hh>
#include <maxscale/modinfo.h>
#include <maxscale/routingworker.hh>

#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/dh.h>

struct DCB;
class MXS_CONFIG_PARAMETER;

enum ssl_method_type_t
{
    SERVICE_TLS10,
    SERVICE_TLS11,
    SERVICE_TLS12,
    SERVICE_TLS13,
    SERVICE_SSL_MAX,
    SERVICE_TLS_MAX,
    SERVICE_SSL_TLS_MAX,
    SERVICE_SSL_UNKNOWN
};

const char*       ssl_method_type_to_string(ssl_method_type_t method_type);
ssl_method_type_t string_to_ssl_method_type(const char* str);

/**
 * Return codes for SSL authentication checks
 */
#define SSL_AUTH_CHECKS_OK       0
#define SSL_ERROR_CLIENT_NOT_SSL 1
#define SSL_ERROR_ACCEPT_FAILED  2

extern const MXS_ENUM_VALUE ssl_version_values[];

// The concrete implementation of the SSLProvider class (hides the dependency on routingworker.hh)
class SSLProviderImp;

namespace maxscale
{

// SSL configuration
struct SSLConfig
{
    SSLConfig() = default;
    SSLConfig(const MXS_CONFIG_PARAMETER& params);

    // CA must always be defined for non-empty configurations
    bool empty() const
    {
        return ca.empty();
    }

    // Convert to human readable string representation
    std::string to_string() const;

    std::string       key;                          /**< SSL private key */
    std::string       cert;                         /**< SSL certificate */
    std::string       ca;                           /**< SSL CA certificate */
    ssl_method_type_t version = SERVICE_SSL_TLS_MAX;/**< Which TLS version to use */
    int               verify_depth = 9;             /**< SSL certificate verification depth */
    bool              verify_peer = true;           /**< Enable peer certificate verification */
    std::string       cipher;                       /**< Selected TLS cipher */
};

/**
 * The SSLContext is used to aggregate the SSL configuration and data for a particular object.
 */
class SSLContext
{
public:
    SSLContext& operator=(SSLContext&) = delete;
    SSLContext(SSLContext&) = delete;

    /**
     * Create a new SSL configuration
     *
     * @param params Parameters from which the SSL configuration is created from
     *
     * @return A new SSL configuration or nullptr on error
     */
    static std::unique_ptr<SSLContext> create(const MXS_CONFIG_PARAMETER& params);

    /**
     * Opens a new OpenSSL session for this configuration context
     */
    SSL* open() const
    {
        return SSL_new(m_ctx);
    }

    // SSL configuration
    const SSLConfig& config() const
    {
        return m_cfg;
    }

    ~SSLContext();

private:
    SSL_CTX*    m_ctx = nullptr;
    SSL_METHOD* m_method = nullptr;         /**<  SSLv3 or TLS1.0/1.1/1.2 methods
                                             * see: https://www.openssl.org/docs/ssl/SSL_CTX_new.html */
    SSLConfig m_cfg;

    SSLContext(const SSLConfig& cfg);
    bool init();
};

// A SSL connection provider (incoming or outgoing). Used by servers and listeners.
class SSLProvider
{
public:
    SSLProvider& operator=(SSLProvider&) = delete;
    SSLProvider(SSLProvider&) = delete;

    SSLProvider(std::unique_ptr<mxs::SSLContext> context);

    // Return true if SSL is enabled
    bool enabled() const
    {
        return m_context.get();
    }

    // Current configuration, or null if none is set.
    const mxs::SSLConfig* config() const;

    // The context or nullptr if no context is set
    mxs::SSLContext* context() const;

    // NOTE: Do not use this, required by binlogrouter
    void set_context(std::unique_ptr<mxs::SSLContext> ssl);

private:
    std::unique_ptr<mxs::SSLContext> m_context;     /**< SSL context */
};
}
