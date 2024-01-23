/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

/**
 * @file ssl.hh
 *
 * The SSL definitions for MaxScale
 */

#include <maxscale/ccdefs.hh>

#include <memory>
#include <openssl/ossl_typ.h>

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

namespace maxscale
{

/**
 * The SSLContext is used to aggregate the SSL configuration and data for a particular object.
 */
class SSLContext final
{
public:
    SSLContext(SSLContext&&) noexcept;
    SSLContext& operator=(SSLContext&& rhs) noexcept;

    SSLContext& operator=(SSLContext&) = delete;
    SSLContext(SSLContext&) = delete;

    explicit SSLContext(mxb::KeyUsage usage);
    ~SSLContext();

    /**
     * Create a new SSL context and configure it.
     *
     * @param config SSL configuration from which the SSLContext is created from
     * @param usage Is the context for listener (SERVER) or server (CLIENT)
     * @return A new SSL context or nullptr on error
     */
    static std::unique_ptr<SSLContext> create(const mxb::SSLConfig& config, mxb::KeyUsage usage);

    /**
     * Opens a new OpenSSL session for this configuration context
     */
    SSL* open() const;

    // SSL configuration
    const mxb::SSLConfig& config() const
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
    bool configure(const mxb::SSLConfig& config);

private:
    bool init();

    SSL_CTX*       m_ctx {nullptr};
    mxb::SSLConfig m_cfg;
    mxb::KeyUsage  m_usage;
};
}
