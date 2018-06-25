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
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/poll.h>
#include <maxscale/service.h>

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
int ssl_authenticate_client(DCB *dcb, bool is_capable)
{
    const char *user = dcb->user ? dcb->user : "";
    const char *remote = dcb->remote ? dcb->remote : "";
    const char *service = (dcb->service && dcb->service->name) ? dcb->service->name : "";

    if (NULL == dcb->listener || NULL == dcb->listener->ssl)
    {
        /* Not an SSL connection on account of listener configuration */
        return SSL_AUTH_CHECKS_OK;
    }
    /* Now we require an SSL connection */
    if (!is_capable)
    {
        /* Should be SSL, but client is not SSL capable */
        MXS_INFO("User %s@%s connected to service '%s' without SSL when SSL was required.",
                 user, remote, service);
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
                     user, remote, service);
            return SSL_ERROR_ACCEPT_FAILED;
        }
        else if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            if (1 == return_code)
            {
                MXS_INFO("User %s@%s connected to service '%s' with SSL.",
                         user, remote, service);
            }
            else
            {
                MXS_INFO("User %s@%s connect to service '%s' with SSL in progress.",
                         user, remote, service);
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
bool
ssl_is_connection_healthy(DCB *dcb)
{
    /**
     * If SSL was never expected, or if the connection has state SSL_ESTABLISHED
     * then everything is as we wish. Otherwise, either there is a problem or
     * more to be done.
     */
    return (NULL == dcb->listener ||
            NULL == dcb->listener->ssl ||
            dcb->ssl_state == SSL_ESTABLISHED);
}

/* Looks to be redundant - can remove include for ioctl too */
bool
ssl_check_data_to_process(DCB *dcb)
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
bool
ssl_required_by_dcb(DCB *dcb)
{
    return NULL != dcb->listener && NULL != dcb->listener->ssl;
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
bool
ssl_required_but_not_negotiated(DCB *dcb)
{
    return (NULL != dcb->listener &&
            NULL != dcb->listener->ssl &&
            SSL_HANDSHAKE_UNKNOWN == dcb->ssl_state);
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

        dprintf(fd, "ssl_verify_peer_certificate=%s\n",
                ssl->ssl_verify_peer_certificate ? "true" : "false");

        const char *version = ssl_method_type_to_string(ssl->ssl_method_type);
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
