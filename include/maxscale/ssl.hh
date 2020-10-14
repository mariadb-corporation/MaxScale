/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
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

#include <memory>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/dh.h>

#include <maxbase/ssl.hh>
#include <maxscale/modinfo.hh>

class DCB;
namespace maxscale
{
class ConfigParameters;
}

/**
 * Return codes for SSL authentication checks
 */
#define SSL_AUTH_CHECKS_OK       0
#define SSL_ERROR_CLIENT_NOT_SSL 1
#define SSL_ERROR_ACCEPT_FAILED  2

extern const MXS_ENUM_VALUE ssl_version_values[];

namespace maxscale
{

// SSL configuration
struct SSLConfig : public mxb::SSLConfig
{
    SSLConfig() = default;
    SSLConfig(const mxs::ConfigParameters& params);

    // Convert to human readable string representation
    std::string to_string() const;
};

/**
 * The SSLContext is used to aggregate the SSL configuration and data for a particular object.
 */
class SSLContext
{
public:
    SSLContext() = default;
    SSLContext(SSLContext&&) noexcept;
    SSLContext& operator=(SSLContext&& rhs) noexcept;

    SSLContext& operator=(SSLContext&) = delete;
    SSLContext(SSLContext&) = delete;
    ~SSLContext();

    /**
     * Create a new SSL configuration
     *
     * @param params Parameters from which the SSL configuration is created from
     *
     * @return A new SSL configuration or nullptr on error
     */
    static std::unique_ptr<SSLContext> create(const mxs::ConfigParameters& params);

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

    bool valid() const
    {
        return m_ctx;
    }

    /**
     * Configure the SSLContext
     *
     * @param params Configuration parameters
     *
     * @return True on success
     */
    bool configure(const mxs::ConfigParameters& params);

private:
    void reset();
    bool init();

    SSL_CTX*    m_ctx {nullptr};
    SSL_METHOD* m_method {nullptr};         /**<  SSLv3 or TLS1.0/1.1/1.2 methods
                                             * see: https://www.openssl.org/docs/ssl/SSL_CTX_new.html */
    SSLConfig m_cfg;
};
}
