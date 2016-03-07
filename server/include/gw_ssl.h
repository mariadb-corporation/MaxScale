#ifndef _GW_SSL_H
#define _GW_SSL_H
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
 * @file gw_ssl.h
 *
 * The SSL definitions for MaxScale
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 27/01/16     Martin Brampton         Initial implementation
 *
 * @endverbatim
 */

#include <gw_protocol.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/dh.h>

struct dcb;

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
 * Return codes for SSL authentication checks
 */
#define SSL_AUTH_CHECKS_OK 0
#define SSL_ERROR_CLIENT_NOT_SSL 1
#define SSL_ERROR_ACCEPT_FAILED 2

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

int ssl_authenticate_client(struct dcb *dcb, const char *user, bool is_capable);
bool ssl_is_connection_healthy(struct dcb *dcb);
bool ssl_check_data_to_process(struct dcb *dcb);
bool ssl_required_by_dcb(struct dcb *dcb);
bool ssl_required_but_not_negotiated(struct dcb *dcb);

#endif /* _GW_SSL_H */