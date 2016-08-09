#ifndef _LISTENER_H
#define _LISTENER_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file listener.h
 *
 * The listener definitions for MaxScale
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 19/01/16     Martin Brampton         Initial implementation
 *
 * @endverbatim
 */

#include <gw_protocol.h>
#include <gw_ssl.h>

struct dcb;

/**
 * The servlistener structure is used to link a service to the protocols that
 * are used to support that service. It defines the name of the protocol module
 * that should be loaded to support the client connection and the port that the
 * protocol should use to listen for incoming client connections.
 */
typedef struct servlistener
{
    char *protocol;             /**< Protocol module to load */
    unsigned short port;        /**< Port to listen on */
    char *address;              /**< Address to listen with */
    char *authenticator;        /**< Name of authenticator */
    SSL_LISTENER *ssl;          /**< Structure of SSL data or NULL */
    struct dcb *listener;       /**< The DCB for the listener */
    struct  servlistener *next; /**< Next service protocol */
} SERV_LISTENER;

SERV_LISTENER *listener_alloc(char *protocol, char *address, unsigned short port, char *authenticator,
                              SSL_LISTENER *ssl);
int listener_set_ssl_version(SSL_LISTENER *ssl_listener, char* version);
void listener_set_certificates(SSL_LISTENER *ssl_listener, char* cert, char* key, char* ca_cert);
int listener_init_SSL(SSL_LISTENER *ssl_listener);

#endif
