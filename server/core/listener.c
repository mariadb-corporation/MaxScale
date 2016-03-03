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
 * @file listener.c  -  Listener generic functions
 *
 * Listeners wait for new client connections and, if the connection is successful
 * a new session is created. A listener typically knows about a port or a socket,
 * and a few other things. It may know about SSL if it is expecting an SSL
 * connection.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 26/01/16     Martin Brampton         Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <listener.h>
#include <gw_ssl.h>
#include <gw_protocol.h>
#include <log_manager.h>

static RSA *rsa_512 = NULL;
static RSA *rsa_1024 = NULL;

static RSA *tmp_rsa_callback(SSL *s, int is_export, int keylength);

/**
 * Create a new listener structure
 *
 * @param protocol      The name of the protocol module
 * @param address       The address to listen with
 * @param port          The port to listen on
 * @param authenticator Name of the authenticator to be used
 * @param ssl           SSL configuration
 * @return      New listener object or NULL if unable to allocate
 */
SERV_LISTENER *
listener_alloc(char *protocol, char *address, unsigned short port, char *authenticator, SSL_LISTENER *ssl)
{
    SERV_LISTENER   *proto = NULL;
    if ((proto = (SERV_LISTENER *)malloc(sizeof(SERV_LISTENER))) != NULL)
    {
        proto->listener = NULL;
        proto->protocol = strdup(protocol);
        proto->address = address ? strdup(address) : NULL;
        proto->port = port;
        proto->authenticator = authenticator ? strdup(authenticator) : NULL;
        proto->ssl = ssl;
    }
    return proto;
}

/**
 * Set the maximum SSL/TLS version the listener will support
 * @param ssl_listener Listener data to configure
 * @param version SSL/TLS version string
 * @return  0 on success, -1 on invalid version string
 */
int
listener_set_ssl_version(SSL_LISTENER *ssl_listener, char* version)
{
    if (strcasecmp(version,"SSLV3") == 0)
    {
        ssl_listener->ssl_method_type = SERVICE_SSLV3;
    }
    else if (strcasecmp(version,"TLSV10") == 0)
    {
        ssl_listener->ssl_method_type = SERVICE_TLS10;
    }
#ifdef OPENSSL_1_0
    else if (strcasecmp(version,"TLSV11") == 0)
    {
        ssl_listener->ssl_method_type = SERVICE_TLS11;
    }
    else if (strcasecmp(version,"TLSV12") == 0)
    {
        ssl_listener->ssl_method_type = SERVICE_TLS12;
    }
#endif
    else if (strcasecmp(version,"MAX") == 0)
    {
        ssl_listener->ssl_method_type = SERVICE_SSL_TLS_MAX;
    }
    else
    {
        return -1;
    }
    return 0;
}

/**
 * Set the locations of the listener's SSL certificate, listener's private key
 * and the CA certificate which both the client and the listener should trust.
 * @param ssl_listener Listener data to configure
 * @param cert SSL certificate
 * @param key SSL private key
 * @param ca_cert SSL CA certificate
 */
void
listener_set_certificates(SSL_LISTENER *ssl_listener, char* cert, char* key, char* ca_cert)
{
    free(ssl_listener->ssl_cert);
    ssl_listener->ssl_cert = cert ? strdup(cert) : NULL;

    free(ssl_listener->ssl_key);
    ssl_listener->ssl_key = key ? strdup(key) : NULL;

    free(ssl_listener->ssl_ca_cert);
    ssl_listener->ssl_ca_cert = ca_cert ? strdup(ca_cert) : NULL;
}

/**
 * Initialize the listener's SSL context. This sets up the generated RSA
 * encryption keys, chooses the listener encryption level and configures the
 * listener certificate, private key and certificate authority file.
 * @param ssl_listener Listener data to initialize
 * @return 0 on success, -1 on error
 */
int
listener_init_SSL(SSL_LISTENER *ssl_listener)
{
    DH* dh;
    RSA* rsa;

    if (!ssl_listener->ssl_init_done)
    {
        switch(ssl_listener->ssl_method_type)
        {
        case SERVICE_SSLV3:
            ssl_listener->method = (SSL_METHOD*)SSLv3_server_method();
            break;
        case SERVICE_TLS10:
            ssl_listener->method = (SSL_METHOD*)TLSv1_server_method();
            break;
#ifdef OPENSSL_1_0
        case SERVICE_TLS11:
            ssl_listener->method = (SSL_METHOD*)TLSv1_1_server_method();
            break;
        case SERVICE_TLS12:
            ssl_listener->method = (SSL_METHOD*)TLSv1_2_server_method();
            break;
#endif
            /** Rest of these use the maximum available SSL/TLS methods */
        case SERVICE_SSL_MAX:
            ssl_listener->method = (SSL_METHOD*)SSLv23_server_method();
            break;
        case SERVICE_TLS_MAX:
            ssl_listener->method = (SSL_METHOD*)SSLv23_server_method();
            break;
        case SERVICE_SSL_TLS_MAX:
            ssl_listener->method = (SSL_METHOD*)SSLv23_server_method();
            break;
        default:
            ssl_listener->method = (SSL_METHOD*)SSLv23_server_method();
            break;
        }

        if ((ssl_listener->ctx = SSL_CTX_new(ssl_listener->method)) == NULL)
        {
            MXS_ERROR("SSL context initialization failed.");
            return -1;
        }

        /** Enable all OpenSSL bug fixes */
        SSL_CTX_set_options(ssl_listener->ctx,SSL_OP_ALL);

        /** Generate the 512-bit and 1024-bit RSA keys */
        if (rsa_512 == NULL)
        {
            rsa_512 = RSA_generate_key(512,RSA_F4,NULL,NULL);
            if (rsa_512 == NULL)
            {
                MXS_ERROR("512-bit RSA key generation failed.");
                return -1;
            }
        }
        if (rsa_1024 == NULL)
        {
            rsa_1024 = RSA_generate_key(1024,RSA_F4,NULL,NULL);
            if (rsa_1024 == NULL)
            {
                MXS_ERROR("1024-bit RSA key generation failed.");
                return -1;
            }
        }

        if (rsa_512 != NULL && rsa_1024 != NULL)
        {
            SSL_CTX_set_tmp_rsa_callback(ssl_listener->ctx,tmp_rsa_callback);
        }

        /** Load the server certificate */
        if (SSL_CTX_use_certificate_file(ssl_listener->ctx, ssl_listener->ssl_cert, SSL_FILETYPE_PEM) <= 0)
        {
            MXS_ERROR("Failed to set server SSL certificate.");
            return -1;
        }

        /* Load the private-key corresponding to the server certificate */
        if (SSL_CTX_use_PrivateKey_file(ssl_listener->ctx, ssl_listener->ssl_key, SSL_FILETYPE_PEM) <= 0)
        {
            MXS_ERROR("Failed to set server SSL key.");
            return -1;
        }

        /* Check if the server certificate and private-key matches */
        if (!SSL_CTX_check_private_key(ssl_listener->ctx))
        {
            MXS_ERROR("Server SSL certificate and key do not match.");
            return -1;
        }

        /* Load the RSA CA certificate into the SSL_CTX structure */
        if (!SSL_CTX_load_verify_locations(ssl_listener->ctx, ssl_listener->ssl_ca_cert, NULL))
        {
            MXS_ERROR("Failed to set Certificate Authority file.");
            return -1;
        }

        /* Set to require peer (client) certificate verification */
        SSL_CTX_set_verify(ssl_listener->ctx,SSL_VERIFY_PEER,NULL);

        /* Set the verification depth */
        SSL_CTX_set_verify_depth(ssl_listener->ctx,ssl_listener->ssl_cert_verify_depth);
        ssl_listener->ssl_init_done = true;
    }
    return 0;
}

/**
 * The RSA key generation callback function for OpenSSL.
 * @param s SSL structure
 * @param is_export Not used
 * @param keylength Length of the key
 * @return Pointer to RSA structure
 */
static RSA *
tmp_rsa_callback(SSL *s, int is_export, int keylength)
{
    RSA *rsa_tmp=NULL;

    switch (keylength) {
    case 512:
        if (rsa_512)
        {
            rsa_tmp = rsa_512;
        }
        else
        {
            /* generate on the fly, should not happen in this example */
            rsa_tmp = RSA_generate_key(keylength,RSA_F4,NULL,NULL);
            rsa_512 = rsa_tmp; /* Remember for later reuse */
        }
        break;
    case 1024:
        if (rsa_1024)
        {
            rsa_tmp=rsa_1024;
        }
        break;
    default:
        /* Generating a key on the fly is very costly, so use what is there */
        if (rsa_1024)
        {
            rsa_tmp=rsa_1024;
        }
        else
        {
            rsa_tmp=rsa_512; /* Use at least a shorter key */
        }
    }
    return(rsa_tmp);
}
