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

#include <maxscale/listener.hh>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include <algorithm>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <maxscale/paths.h>
#include <maxscale/ssl.h>
#include <maxscale/protocol.h>
#include <maxscale/log.h>
#include <maxscale/alloc.h>
#include <maxscale/users.h>
#include <maxscale/service.hh>
#include <maxscale/poll.h>

static std::list<SListener> all_listeners;
static std::mutex listener_lock;

static RSA* rsa_512 = NULL;
static RSA* rsa_1024 = NULL;
static RSA* tmp_rsa_callback(SSL* s, int is_export, int keylength);

SERV_LISTENER::SERV_LISTENER(SERVICE* service, const std::string& name, const std::string& address,
                             uint16_t port, const std::string& protocol, const std::string& authenticator,
                             const std::string& auth_opts, void* auth_instance, SSL_LISTENER* ssl)
    : name(name)
    , protocol(protocol)
    , port(port)
    , address(address)
    , authenticator(authenticator)
    , auth_options(auth_opts)
    , auth_instance(auth_instance)
    , ssl(ssl)
    , listener(nullptr)
    , users(nullptr)
    , service(service)
    , active(1)
    , next(nullptr)
{
}

SERV_LISTENER::~SERV_LISTENER()
{
    if (users)
    {
        users_free(users);
    }

    if (listener)
    {
        dcb_close(listener);
    }

    SSL_LISTENER_free(ssl);
}

SERV_LISTENER* listener_alloc(SERVICE* service,
                              const char* name,
                              const char* protocol,
                              const char* address,
                              unsigned short port,
                              const char* authenticator,
                              const char* auth_options,
                              SSL_LISTENER* ssl)
{
    if (!authenticator)
    {
        if ((authenticator = get_default_authenticator(protocol)) == NULL)
        {
            MXS_ERROR("No authenticator defined for listener '%s' and could not get "
                      "default authenticator for protocol '%s'.", name, protocol);
            return nullptr;
        }
    }

    void* auth_instance = NULL;

    if (!authenticator_init(&auth_instance, authenticator, auth_options))
    {
        MXS_ERROR("Failed to initialize authenticator module '%s' for listener '%s'.",
                  authenticator, name);
        return nullptr;
    }

    auto listener = new(std::nothrow) SERV_LISTENER(service, name, address, port, protocol, authenticator,
                                                    auth_options, auth_instance, ssl);

    if (listener)
    {
        std::lock_guard<std::mutex> guard(listener_lock);
        all_listeners.emplace_back(listener);
    }

    return listener;
}

void listener_free(SERV_LISTENER* listener)
{
    std::lock_guard<std::mutex> guard(listener_lock);
    all_listeners.remove_if([&](const SListener& l) {
                                return l.get() == listener;
                            });
}

void listener_destroy(SERV_LISTENER* listener)
{
    listener_set_active(listener, false);
    listener_stop(listener);

    // TODO: This is not pretty but it works, revise when listeners are refactored. This is
    // thread-safe as the listener is freed on the same thread that closes the socket.
    close(listener->listener->fd);
    listener->listener->fd = -1;
}

bool listener_stop(SERV_LISTENER* listener)
{
    bool rval = false;
    mxb_assert(listener->listener);

    if (listener->listener->session->state == SESSION_STATE_LISTENER
        && poll_remove_dcb(listener->listener) == 0)
    {
        listener->listener->session->state = SESSION_STATE_LISTENER_STOPPED;
        rval = true;
    }

    return rval;
}

bool listener_start(SERV_LISTENER* listener)
{
    bool rval = true;
    mxb_assert(listener->listener);

    if (listener->listener->session->state == SESSION_STATE_LISTENER_STOPPED
        && poll_add_dcb(listener->listener) == 0)
    {
        listener->listener->session->state = SESSION_STATE_LISTENER;
        rval = true;
    }

    return rval;
}

/**
 * Set the maximum SSL/TLS version the listener will support
 * @param ssl_listener Listener data to configure
 * @param version SSL/TLS version string
 * @return  0 on success, -1 on invalid version string
 */
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

/**
 * Set the locations of the listener's SSL certificate, listener's private key
 * and the CA certificate which both the client and the listener should trust.
 * @param ssl_listener Listener data to configure
 * @param cert SSL certificate
 * @param key SSL private key
 * @param ca_cert SSL CA certificate
 */
void listener_set_certificates(SSL_LISTENER* ssl_listener, char* cert, char* key, char* ca_cert)
{
    MXS_FREE(ssl_listener->ssl_cert);
    ssl_listener->ssl_cert = cert ? MXS_STRDUP_A(cert) : NULL;

    MXS_FREE(ssl_listener->ssl_key);
    ssl_listener->ssl_key = key ? MXS_STRDUP_A(key) : NULL;

    MXS_FREE(ssl_listener->ssl_ca_cert);
    ssl_listener->ssl_ca_cert = ca_cert ? MXS_STRDUP_A(ca_cert) : NULL;
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

/**
 * Creates a listener configuration at the location pointed by @c filename
 *
 * @param listener Listener to serialize into a configuration
 * @param filename Filename where configuration is written
 * @return True on success, false on error
 */
static bool create_listener_config(const SERV_LISTENER* listener, const char* filename)
{
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing listener '%s': %d, %s",
                  filename,
                  listener->name.c_str(),
                  errno,
                  mxs_strerror(errno));
        return false;
    }

    // TODO: Check for return values on all of the dprintf calls
    dprintf(file, "[%s]\n", listener->name.c_str());
    dprintf(file, "type=listener\n");
    dprintf(file, "protocol=%s\n", listener->protocol.c_str());
    dprintf(file, "service=%s\n", listener->service->name);
    dprintf(file, "address=%s\n", listener->address.c_str());
    dprintf(file, "port=%u\n", listener->port);
    dprintf(file, "authenticator=%s\n", listener->authenticator.c_str());

    if (!listener->auth_options.empty())
    {
        dprintf(file, "authenticator_options=%s\n", listener->auth_options.c_str());
    }

    if (listener->ssl)
    {
        write_ssl_config(file, listener->ssl);
    }

    close(file);

    return true;
}

bool listener_serialize(const SERV_LISTENER* listener)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename,
             sizeof(filename),
             "%s/%s.cnf.tmp",
             get_config_persistdir(),
             listener->name.c_str());

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary listener configuration at '%s': %d, %s",
                  filename,
                  errno,
                  mxs_strerror(errno));
    }
    else if (create_listener_config(listener, filename))
    {
        char final_filename[PATH_MAX];
        strcpy(final_filename, filename);

        char* dot = strrchr(final_filename, '.');
        mxb_assert(dot);
        *dot = '\0';

        if (rename(filename, final_filename) == 0)
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to rename temporary listener configuration at '%s': %d, %s",
                      filename,
                      errno,
                      mxs_strerror(errno));
        }
    }

    return rval;
}

json_t* listener_to_json(const SERV_LISTENER* listener)
{
    json_t* param = json_object();
    json_object_set_new(param, "address", json_string(listener->address.c_str()));
    json_object_set_new(param, "port", json_integer(listener->port));
    json_object_set_new(param, "protocol", json_string(listener->protocol.c_str()));
    json_object_set_new(param, "authenticator", json_string(listener->authenticator.c_str()));
    json_object_set_new(param, "auth_options", json_string(listener->auth_options.c_str()));

    if (listener->ssl)
    {
        json_t* ssl = json_object();

        const char* ssl_method = ssl_method_type_to_string(listener->ssl->ssl_method_type);
        json_object_set_new(ssl, "ssl_version", json_string(ssl_method));
        json_object_set_new(ssl, "ssl_cert", json_string(listener->ssl->ssl_cert));
        json_object_set_new(ssl, "ssl_ca_cert", json_string(listener->ssl->ssl_ca_cert));
        json_object_set_new(ssl, "ssl_key", json_string(listener->ssl->ssl_key));

        json_object_set_new(param, "ssl", ssl);
    }

    json_t* attr = json_object();
    json_object_set_new(attr, CN_STATE, json_string(listener_state_to_string(listener)));
    json_object_set_new(attr, CN_PARAMETERS, param);

    if (listener->listener->authfunc.diagnostic_json)
    {
        json_t* diag = listener->listener->authfunc.diagnostic_json(listener);

        if (diag)
        {
            json_object_set_new(attr, CN_AUTHENTICATOR_DIAGNOSTICS, diag);
        }
    }

    json_t* rval = json_object();
    json_object_set_new(rval, CN_ID, json_string(listener->name.c_str()));
    json_object_set_new(rval, CN_TYPE, json_string(CN_LISTENERS));
    json_object_set_new(rval, CN_ATTRIBUTES, attr);

    return rval;
}

void listener_set_active(SERV_LISTENER* listener, bool active)
{
    atomic_store_int32(&listener->active, active ? 1 : 0);
}

bool listener_is_active(SERV_LISTENER* listener)
{
    return atomic_load_int32(&listener->active);
}

static inline SERV_LISTENER* load_port(SERV_LISTENER const* const* const port)
{
    return (SERV_LISTENER*)atomic_load_ptr((void**)port);
}

SERV_LISTENER* listener_iterator_init(const SERVICE* service, LISTENER_ITERATOR* iter)
{
    mxb_assert(iter);
    iter->current = load_port(&service->ports);
    return iter->current;
}

SERV_LISTENER* listener_iterator_next(LISTENER_ITERATOR* iter)
{
    mxb_assert(iter);

    if (iter->current)
    {
        iter->current = load_port(&iter->current->next);
    }

    return iter->current;
}

const char* listener_state_to_string(const SERV_LISTENER* listener)
{
    mxb_assert(listener);

    if (listener->listener && listener->listener->session)
    {
        switch (listener->listener->session->state)
        {
        case SESSION_STATE_LISTENER_STOPPED:
            return "Stopped";

        case SESSION_STATE_LISTENER:
            return "Running";

        default:
            mxb_assert(!true);
            return "Unknown";
        }
    }
    else
    {
        return "Failed";
    }
}
