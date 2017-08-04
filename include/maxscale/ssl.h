#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file ssl.h
 *
 * The SSL definitions for MaxScale
 */

#include <maxscale/cdefs.h>
#include <maxscale/protocol.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/dh.h>

MXS_BEGIN_DECLS

struct dcb;

typedef enum ssl_method_type
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
    SERVICE_SSL_TLS_MAX
} ssl_method_type_t;

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
    ssl_method_type_t ssl_method_type;  /*< Which of the SSLv3 or TLS1.0/1.1/1.2 methods to use */
    char *ssl_cert;                     /*< SSL certificate */
    char *ssl_key;                      /*< SSL private key */
    char *ssl_ca_cert;                  /*< SSL CA certificate */
    bool ssl_init_done;                 /*< If SSL has already been initialized for this service */
    struct ssl_listener
        *next;          /*< Next SSL configuration, currently used to store obsolete configurations */
} SSL_LISTENER;

int ssl_authenticate_client(struct dcb *dcb, bool is_capable);
bool ssl_is_connection_healthy(struct dcb *dcb);
bool ssl_check_data_to_process(struct dcb *dcb);
bool ssl_required_by_dcb(struct dcb *dcb);
bool ssl_required_but_not_negotiated(struct dcb *dcb);
const char* ssl_method_type_to_string(ssl_method_type_t method_type);

/**
 * Helper function for client ssl authentication. Authenticates and checks changes
 * in ssl status.
 *
 * @param dcb Client dcb
 *
 * @return MXS_AUTH_FAILED_SSL or MXS_AUTH_FAILED on error. MXS_AUTH_SSL_INCOMPLETE
 * if ssl authentication is in progress and should be retried, MXS_AUTH_SSL_COMPLETE
 * if ssl authentication is complete or not required.
 */
int ssl_authenticate_check_status(struct dcb *dcb);

MXS_END_DECLS
