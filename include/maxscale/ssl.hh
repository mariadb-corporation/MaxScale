/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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

#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/dh.h>

struct DCB;
class MXS_CONFIG_PARAMETER;

enum ssl_method_type_t
{
#ifndef OPENSSL_1_1
    SERVICE_TLS10,
#endif
#ifdef OPENSSL_1_0
    SERVICE_TLS11,
    SERVICE_TLS12,
#endif
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

namespace maxscale
{

/**
 * The SSLContext is used to aggregate the SSL configuration and data for a particular object.
 */
class SSLContext
{
public:
    /**
     * Create a new SSL configuration
     *
     * @param params Parameters from which the SSL configuration is created from
     *
     * @return A new SSL configuration or nullptr on error
     */
    static std::unique_ptr<SSLContext> create(const MXS_CONFIG_PARAMETER& params);

    /**
     * Serialize the SSL configuration into a INI file section
     *
     * @return SSLContext as a INI file section
     */
    std::string serialize() const;

    /**
     * Opens a new OpenSSL session for this configuration context
     */
    SSL* open() const
    {
        return SSL_new(m_ctx);
    }

    // Private key
    const std::string& ssl_key() const
    {
        return m_key;
    }

    // Public cert
    const std::string& ssl_cert() const
    {
        return m_cert;
    }

    // Certificate authority
    const std::string& ssl_ca() const
    {
        return m_ca;
    }

    // Convert to JSON representation
    json_t* to_json() const;

    // Convert to human readable string representation
    std::string to_string() const;

    ~SSLContext();

private:
    SSL_CTX*    m_ctx = nullptr;
    SSL_METHOD* m_method = nullptr;         /**<  SSLv3 or TLS1.0/1.1/1.2 methods
                                             * see: https://www.openssl.org/docs/ssl/SSL_CTX_new.html */

    std::string       m_key;            /**< SSL private key */
    std::string       m_cert;           /**< SSL certificate */
    std::string       m_ca;             /**< SSL CA certificate */
    ssl_method_type_t m_version;        /**< Which TLS version to use */
    int               m_verify_depth;   /**< SSL certificate verification depth */
    bool              m_verify_peer;    /**< Enable peer certificate verification */

    SSLContext(const std::string& key, const std::string& cert, const std::string& ca,
               ssl_method_type_t version, int verify_depth, bool verify_peer_cert);
    bool init();
};
}
