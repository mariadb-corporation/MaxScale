/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

static RSA* rsa_512 = NULL;
static RSA* rsa_1024 = NULL;
static RSA* tmp_rsa_callback(SSL* s, int is_export, int keylength);

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
#ifndef OPENSSL_1_1
    case SERVICE_TLS10:
        return "TLSV10";

#endif
#ifdef OPENSSL_1_0
    case SERVICE_TLS11:
        return "TLSV11";

    case SERVICE_TLS12:
        return "TLSV12";

#endif
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

#ifndef OPENSSL_1_1
    else if (strcasecmp("TLSV10", str) == 0)
    {
        return SERVICE_TLS10;
    }
#endif
#ifdef OPENSSL_1_0
    else if (strcasecmp("TLSV11", str) == 0)
    {
        return SERVICE_TLS11;
    }
    else if (strcasecmp("TLSV12", str) == 0)
    {
        return SERVICE_TLS12;
    }
#endif

    return SERVICE_SSL_UNKNOWN;
}

void write_ssl_config(int fd, mxs::SSLContext* ssl)
{
    if (ssl)
    {
        dprintf(fd, "ssl=required\n");

        if (ssl->ssl_cert)
        {
            dprintf(fd, "ssl_cert=%s\n", ssl->ssl_cert);
        }

        if (ssl->ssl_key)
        {
            dprintf(fd, "ssl_key=%s\n", ssl->ssl_key);
        }

        if (ssl->ssl_ca_cert)
        {
            dprintf(fd, "ssl_ca_cert=%s\n", ssl->ssl_ca_cert);
        }
        if (ssl->ssl_cert_verify_depth)
        {
            dprintf(fd, "ssl_cert_verify_depth=%d\n", ssl->ssl_cert_verify_depth);
        }

        dprintf(fd,
                "ssl_verify_peer_certificate=%s\n",
                ssl->ssl_verify_peer_certificate ? "true" : "false");

        const char* version = ssl_method_type_to_string(ssl->ssl_method_type);
        dprintf(fd, "ssl_version=%s\n", version);
    }
}

int listener_set_ssl_version(mxs::SSLContext* ssl_listener, const char* version)
{
    if (strcasecmp(version, "MAX") == 0)
    {
        ssl_listener->ssl_method_type = SERVICE_SSL_TLS_MAX;
    }
#ifndef OPENSSL_1_1
    else if (strcasecmp(version, "TLSV10") == 0)
    {
        ssl_listener->ssl_method_type = SERVICE_TLS10;
    }
#else
#endif
#ifdef OPENSSL_1_0
    else if (strcasecmp(version, "TLSV11") == 0)
    {
        ssl_listener->ssl_method_type = SERVICE_TLS11;
    }
    else if (strcasecmp(version, "TLSV12") == 0)
    {
        ssl_listener->ssl_method_type = SERVICE_TLS12;
    }
#endif
    else
    {
        return -1;
    }
    return 0;
}

void listener_set_certificates(mxs::SSLContext* ssl_listener, const std::string& cert,
                               const std::string& key, const std::string& ca_cert)
{
    MXS_FREE(ssl_listener->ssl_cert);
    ssl_listener->ssl_cert = !cert.empty() ? MXS_STRDUP_A(cert.c_str()) : nullptr;

    MXS_FREE(ssl_listener->ssl_key);
    ssl_listener->ssl_key = !key.empty() ? MXS_STRDUP_A(key.c_str()) : nullptr;

    MXS_FREE(ssl_listener->ssl_ca_cert);
    ssl_listener->ssl_ca_cert = !ca_cert.empty() ? MXS_STRDUP_A(ca_cert.c_str()) : nullptr;
}

RSA* create_rsa(int bits)
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

bool SSL_LISTENER_init(mxs::SSLContext* ssl)
{
    mxb_assert(!ssl->ssl_init_done);
    bool rval = true;

    switch (ssl->ssl_method_type)
    {
#ifndef OPENSSL_1_1
    case SERVICE_TLS10:
        ssl->method = (SSL_METHOD*)TLSv1_method();
        break;

#endif
#ifdef OPENSSL_1_0
    case SERVICE_TLS11:
        ssl->method = (SSL_METHOD*)TLSv1_1_method();
        break;

    case SERVICE_TLS12:
        ssl->method = (SSL_METHOD*)TLSv1_2_method();
        break;

#endif
    /** Rest of these use the maximum available SSL/TLS methods */
    case SERVICE_SSL_MAX:
        ssl->method = (SSL_METHOD*)SSLv23_method();
        break;

    case SERVICE_TLS_MAX:
        ssl->method = (SSL_METHOD*)SSLv23_method();
        break;

    case SERVICE_SSL_TLS_MAX:
        ssl->method = (SSL_METHOD*)SSLv23_method();
        break;

    default:
        ssl->method = (SSL_METHOD*)SSLv23_method();
        break;
    }

    SSL_CTX* ctx = SSL_CTX_new(ssl->method);

    if (ctx == NULL)
    {
        MXS_ERROR("SSL context initialization failed: %s", get_ssl_errors());
        return false;
    }

    SSL_CTX_set_default_read_ahead(ctx, 0);

    /** Enable all OpenSSL bug fixes */
    SSL_CTX_set_options(ctx, SSL_OP_ALL);

    /** Disable SSLv3 */
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3);

    // Disable session cache
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);

    //
    // Note: This is not safe if SSL initialization is done concurrently
    //
    /** Generate the 512-bit and 1024-bit RSA keys */
    if (rsa_512 == NULL && (rsa_512 = create_rsa(512)) == NULL)
    {
        MXS_ERROR("512-bit RSA key generation failed.");
        rval = false;
    }
    else if (rsa_1024 == NULL && (rsa_1024 = create_rsa(1024)) == NULL)
    {
        MXS_ERROR("1024-bit RSA key generation failed.");
        rval = false;
    }
    else
    {
        mxb_assert(rsa_512 && rsa_1024);
        SSL_CTX_set_tmp_rsa_callback(ctx, tmp_rsa_callback);
    }

    mxb_assert(ssl->ssl_ca_cert);

    /* Load the CA certificate into the SSL_CTX structure */
    if (!SSL_CTX_load_verify_locations(ctx, ssl->ssl_ca_cert, NULL))
    {
        MXS_ERROR("Failed to set Certificate Authority file");
        rval = false;
    }

    if (ssl->ssl_cert && ssl->ssl_key)
    {
        /** Load the server certificate */
        if (SSL_CTX_use_certificate_chain_file(ctx, ssl->ssl_cert) <= 0)
        {
            MXS_ERROR("Failed to set server SSL certificate: %s", get_ssl_errors());
            rval = false;
        }

        /* Load the private-key corresponding to the server certificate */
        if (SSL_CTX_use_PrivateKey_file(ctx, ssl->ssl_key, SSL_FILETYPE_PEM) <= 0)
        {
            MXS_ERROR("Failed to set server SSL key: %s", get_ssl_errors());
            rval = false;
        }

        /* Check if the server certificate and private-key matches */
        if (!SSL_CTX_check_private_key(ctx))
        {
            MXS_ERROR("Server SSL certificate and key do not match: %s", get_ssl_errors());
            rval = false;
        }
    }

    /* Set to require peer (client) certificate verification */
    if (ssl->ssl_verify_peer_certificate)
    {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    }

    /* Set the verification depth */
    SSL_CTX_set_verify_depth(ctx, ssl->ssl_cert_verify_depth);

    if (rval)
    {
        ssl->ssl_init_done = true;
        ssl->ctx = ctx;
    }
    else
    {
        SSL_CTX_free(ctx);
    }

    return rval;
}

void SSL_LISTENER_free(mxs::SSLContext* ssl)
{
    if (ssl)
    {
        SSL_CTX_free(ssl->ctx);
        MXS_FREE(ssl->ssl_ca_cert);
        MXS_FREE(ssl->ssl_cert);
        MXS_FREE(ssl->ssl_key);
        MXS_FREE(ssl);
    }
}
