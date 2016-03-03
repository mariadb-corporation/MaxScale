#ifndef _LISTENER_H
#define _LISTENER_H
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
#include <dcb.h>

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
    DCB *listener;              /**< The DCB for the listener */
    struct  servlistener *next; /**< Next service protocol */
} SERV_LISTENER;

SERV_LISTENER *listener_alloc(char *protocol, char *address, unsigned short port, char *authenticator, SSL_LISTENER *ssl);
int listener_set_ssl_version(SSL_LISTENER *ssl_listener, char* version);
void listener_set_certificates(SSL_LISTENER *ssl_listener, char* cert, char* key, char* ca_cert);
int listener_init_SSL(SSL_LISTENER *ssl_listener);

#endif