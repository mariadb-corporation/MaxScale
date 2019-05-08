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
 * @brief Check client's SSL capability and start SSL if appropriate.
 *
 * The protocol should determine whether the client is SSL capable and pass
 * the result as the second parameter. If the listener requires SSL but the
 * client is not SSL capable, an error message is recorded and failure return
 * given. If both sides want SSL, and SSL is not already established, the
 * process is triggered by calling dcb_accept_SSL.
 *
 * @param dcb Request handler DCB connected to the client
 * @param is_capable Indicates if the client can handle SSL
 * @return 0 if ok, >0 if a problem - see return codes defined in ssl.h
 */
int ssl_authenticate_client(DCB* dcb, bool is_capable)
{
    const char* user = dcb->user ? dcb->user : "";
    const char* remote = dcb->remote ? dcb->remote : "";
    const char* service = (dcb->service && dcb->service->name()) ? dcb->service->name() : "";

    if (NULL == dcb->session->listener || NULL == dcb->session->listener->ssl())
    {
        /* Not an SSL connection on account of listener configuration */
        return SSL_AUTH_CHECKS_OK;
    }
    /* Now we require an SSL connection */
    if (!is_capable)
    {
        /* Should be SSL, but client is not SSL capable */
        MXS_INFO("User %s@%s connected to service '%s' without SSL when SSL was required.",
                 user,
                 remote,
                 service);
        return SSL_ERROR_CLIENT_NOT_SSL;
    }
    /* Now we know SSL is required and client is capable */
    if (dcb->ssl_state != SSL_HANDSHAKE_DONE && dcb->ssl_state != SSL_ESTABLISHED)
    {
        int return_code;
        /** Do the SSL Handshake */
        if (SSL_HANDSHAKE_UNKNOWN == dcb->ssl_state)
        {
            dcb->ssl_state = SSL_HANDSHAKE_REQUIRED;
        }
        /**
         * Note that this will often fail to achieve its result, because further
         * reading (or possibly writing) of SSL related information is needed.
         * When that happens, there is a call in poll.c so that an EPOLLIN
         * event that arrives while the SSL state is SSL_HANDSHAKE_REQUIRED
         * will trigger dcb_accept_SSL. This situation does not result in a
         * negative return code - that indicates a real failure.
         */
        return_code = dcb_accept_SSL(dcb);
        if (return_code < 0)
        {
            MXS_INFO("User %s@%s failed to connect to service '%s' with SSL.",
                     user,
                     remote,
                     service);
            return SSL_ERROR_ACCEPT_FAILED;
        }
        else if (mxs_log_is_priority_enabled(LOG_INFO))
        {
            if (1 == return_code)
            {
                MXS_INFO("User %s@%s connected to service '%s' with SSL.",
                         user,
                         remote,
                         service);
            }
            else
            {
                MXS_INFO("User %s@%s connect to service '%s' with SSL in progress.",
                         user,
                         remote,
                         service);
            }
        }
    }
    return SSL_AUTH_CHECKS_OK;
}

/**
 * @brief If an SSL connection is required, check that it has been established.
 *
 * This is called at the end of the authentication of a new connection.
 * If the result is not true, the data packet is abandoned with further
 * data expected from the client.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean to indicate whether connection is healthy
 */
bool ssl_is_connection_healthy(DCB* dcb)
{
    /**
     * If SSL was never expected, or if the connection has state SSL_ESTABLISHED
     * then everything is as we wish. Otherwise, either there is a problem or
     * more to be done.
     */
    return NULL == dcb->session->listener
           || NULL == dcb->session->listener->ssl()
           || dcb->ssl_state == SSL_ESTABLISHED;
}

/* Looks to be redundant - can remove include for ioctl too */
bool ssl_check_data_to_process(DCB* dcb)
{
    /** SSL authentication is still going on, we need to call dcb_accept_SSL
     * until it return 1 for success or -1 for error */
    if (dcb->ssl_state == SSL_HANDSHAKE_REQUIRED && 1 == dcb_accept_SSL(dcb))
    {
        int b = 0;
        ioctl(dcb->fd, FIONREAD, &b);
        if (b != 0)
        {
            return true;
        }
        else
        {
            MXS_DEBUG("[gw_read_client_event] No data in socket after SSL auth");
        }
    }
    return false;
}

/**
 * @brief Check whether a DCB requires SSL.
 *
 * This is a very simple test, but is placed in an SSL function so that
 * the knowledge of the SSL process is removed from the more general
 * handling of a connection in the protocols.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean indicating whether SSL is required.
 */
bool ssl_required_by_dcb(DCB* dcb)
{
    return NULL != dcb->session->listener && NULL != dcb->session->listener->ssl();
}

/**
 * @brief Check whether a DCB requires SSL, but SSL is not yet negotiated.
 *
 * This is a very simple test, but is placed in an SSL function so that
 * the knowledge of the SSL process is removed from the more general
 * handling of a connection in the protocols.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean indicating whether SSL is required and not negotiated.
 */
bool ssl_required_but_not_negotiated(DCB* dcb)
{
    return NULL != dcb->session->listener
           && NULL != dcb->session->listener->ssl()
           && SSL_HANDSHAKE_UNKNOWN == dcb->ssl_state;
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

void write_ssl_config(int fd, SSL_LISTENER* ssl)
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

int ssl_authenticate_check_status(DCB* dcb)
{
    int rval = MXS_AUTH_FAILED;
    /**
     * We record the SSL status before and after ssl authentication. This allows
     * us to detect if the SSL handshake is immediately completed, which means more
     * data needs to be read from the socket.
     */
    bool health_before = ssl_is_connection_healthy(dcb);
    int ssl_ret = ssl_authenticate_client(dcb, dcb->authfunc.connectssl(dcb));
    bool health_after = ssl_is_connection_healthy(dcb);

    if (ssl_ret != 0)
    {
        rval = (ssl_ret == SSL_ERROR_CLIENT_NOT_SSL) ? MXS_AUTH_FAILED_SSL : MXS_AUTH_FAILED;
    }
    else if (!health_after)
    {
        rval = MXS_AUTH_SSL_INCOMPLETE;
    }
    else if (!health_before && health_after)
    {
        rval = MXS_AUTH_SSL_INCOMPLETE;
        poll_add_epollin_event_to_dcb(dcb, NULL);
    }
    else if (health_before && health_after)
    {
        rval = MXS_AUTH_SSL_COMPLETE;
    }
    return rval;
}

int listener_set_ssl_version(SSL_LISTENER* ssl_listener, const char* version)
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

void listener_set_certificates(SSL_LISTENER* ssl_listener, const std::string& cert,
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

bool SSL_LISTENER_init(SSL_LISTENER* ssl)
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

void SSL_LISTENER_free(SSL_LISTENER* ssl)
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
