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
#include <dcb.h>

enum
{
    SERVICE_SSLV3,
    SERVICE_TLS10,
#ifdef OPENSSL_1_0
    SERVICE_TLS11,
    SERVICE_TLS12,
#endif
    SERVICE_SSL_MAX,
    SERVICE_TLS_MAX,
    SERVICE_SSL_TLS_MAX
};

/**
 * The ssl_listener structure is used to aggregate the SSL configuration items
 * and data for a particular listener
 */
typedef struct ssl_listener
{
    SSL_CTX *ctx;
    SSL_METHOD *method;                 /*<  SSLv3 or TLS1.0/1.1/1.2 methods
                                         * see: https://www.openssl.org/docs/ssl/SSL_CTX_new.html */
    int ssl_cert_verify_depth;          /*< SSL certificate verification depth */
    int ssl_method_type;                /*< Which of the SSLv3 or TLS1.0/1.1/1.2 methods to use */
    char *ssl_cert;                     /*< SSL certificate */
    char *ssl_key;                      /*< SSL private key */
    char *ssl_ca_cert;                  /*< SSL CA certificate */
    bool ssl_init_done;                 /*< If SSL has already been initialized for this service */
} SSL_LISTENER;

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

int listener_set_ssl_version(SSL_LISTENER *ssl_listener, char* version);
void listener_set_certificates(SSL_LISTENER *ssl_listener, char* cert, char* key, char* ca_cert);
int listener_init_SSL(SSL_LISTENER *ssl_listener);

#endif