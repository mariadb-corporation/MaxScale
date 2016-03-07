/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */

/**
 * @file gw_ssl.c  -  SSL generic functions
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
#include <dcb.h>
#include <service.h>
#include <log_manager.h>
#include <sys/ioctl.h>

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
 * @return 0 if ok, >0 if a problem - see return codes defined in gw_ssl.h
 */
int ssl_authenticate_client(DCB *dcb, const char *user, bool is_capable)
{
    char *remote = dcb->remote ? dcb->remote : "";
    char *service = (dcb->service && dcb->service->name) ? dcb->service->name : "";

    if (NULL == dcb->listen_ssl)
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
    return (NULL == dcb->listen_ssl || dcb->ssl_state == SSL_ESTABLISHED);
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
        ioctl(dcb->fd,FIONREAD,&b);
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
    return NULL != dcb->listen_ssl;
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
    return (NULL != dcb->listen_ssl && SSL_HANDSHAKE_UNKNOWN == dcb->ssl_state);
}