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

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include <maxscale/paths.h>
#include <maxscale/ssl.h>
#include <maxscale/protocol.h>
#include <maxscale/log.h>
#include <maxscale/alloc.h>
#include <maxscale/users.h>
#include <maxscale/service.hh>
#include <maxscale/poll.h>
#include <maxscale/routingworker.hh>

#include "internal/modules.hh"

static std::list<SListener> all_listeners;
static std::mutex listener_lock;

static RSA* rsa_512 = NULL;
static RSA* rsa_1024 = NULL;
static RSA* tmp_rsa_callback(SSL* s, int is_export, int keylength);

Listener::Listener(SERVICE* service, const std::string& name, const std::string& address,
                   uint16_t port, const std::string& protocol, const std::string& authenticator,
                   const std::string& auth_opts, void* auth_instance, SSL_LISTENER* ssl)
    : MXB_POLL_DATA{Listener::poll_handler}
    , m_name(name)
    , m_state(CREATED)
    , m_protocol(protocol)
    , m_port(port)
    , m_address(address)
    , m_authenticator(authenticator)
    , m_auth_options(auth_opts)
    , m_auth_instance(auth_instance)
    , m_ssl(ssl)
    , m_users(nullptr)
    , m_service(service)
    , m_proto_func(*(MXS_PROTOCOL*)load_module(protocol.c_str(), MODULE_PROTOCOL))
    , m_auth_func(*(MXS_AUTHENTICATOR*)load_module(authenticator.c_str(), MODULE_AUTHENTICATOR))
{
}

Listener::~Listener()
{
    if (m_users)
    {
        users_free(m_users);
    }

    SSL_LISTENER_free(m_ssl);
}

SListener Listener::create(SERVICE* service,
                           const std::string& name,
                           const std::string& protocol,
                           const std::string& address,
                           unsigned short port,
                           const std::string& authenticator,
                           const std::string& auth_options,
                           SSL_LISTENER* ssl)
{
    const char* auth = !authenticator.empty() ? authenticator.c_str() :
        get_default_authenticator(protocol.c_str());

    if (!auth)
    {
        MXS_ERROR("No authenticator defined for listener '%s' and could not get "
                  "default authenticator for protocol '%s'.", name.c_str(), protocol.c_str());
        return nullptr;
    }

    void* auth_instance = NULL;

    if (!authenticator_init(&auth_instance, auth, auth_options.c_str()))
    {
        MXS_ERROR("Failed to initialize authenticator module '%s' for listener '%s'.",
                  auth, name.c_str());
        return nullptr;
    }

    // Add protocol and authenticator capabilities from the listener
    const MXS_MODULE* proto_mod = get_module(protocol.c_str(), MODULE_PROTOCOL);
    const MXS_MODULE* auth_mod = get_module(auth, MODULE_AUTHENTICATOR);
    mxb_assert(proto_mod && auth_mod);

    SListener listener(new(std::nothrow) Listener(service, name, address, port, protocol, auth,
                                                  auth_options, auth_instance, ssl));

    if (listener)
    {
        // Storing a self-reference to the listener makes it possible to easily
        // increment the reference count when new connections are accepted.
        listener->m_self = listener;

        // Note: This isn't good: we modify the service from a listener and the service itself should do this.
        service->capabilities |= proto_mod->module_capabilities | auth_mod->module_capabilities;

        std::lock_guard<std::mutex> guard(listener_lock);
        all_listeners.push_back(listener);
    }

    return listener;
}

void Listener::destroy(const SListener& listener)
{
    // Remove the listener from all workers. This makes sure that there's no concurrent access while we're
    // closing things up.
    listener->stop();

    close(listener->m_fd);
    listener->m_fd = -1;
    listener->m_state = DESTROYED;

    std::lock_guard<std::mutex> guard(listener_lock);
    all_listeners.remove(listener);
}

bool Listener::stop()
{
    bool rval = (m_state == STOPPED);

    if (m_state == STARTED && mxs::RoutingWorker::remove_shared_fd(m_fd))
    {
        m_state = STOPPED;
        rval = true;
    }

    return rval;
}

bool Listener::start()
{
    bool rval = (m_state == STARTED);

    if (m_state == STOPPED && mxs::RoutingWorker::add_shared_fd(m_fd, EPOLLIN, this))
    {
        m_state = STARTED;
        rval = true;
    }

    return rval;
}

SListener listener_find(const std::string& name)
{
    SListener rval;
    std::lock_guard<std::mutex> guard(listener_lock);

    for (const auto& a : all_listeners)
    {
        if (a->name() == name)
        {
            rval = a;
            break;
        }
    }

    return rval;
}

std::vector<SListener> listener_find_by_service(const SERVICE* service)
{
    std::vector<SListener> rval;
    std::lock_guard<std::mutex> guard(listener_lock);

    for (const auto& a : all_listeners)
    {
        if (a->service() == service)
        {
            rval.push_back(a);
        }
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
bool Listener::create_listener_config(const char* filename)
{
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing listener '%s': %d, %s",
                  filename,
                  m_name.c_str(),
                  errno,
                  mxs_strerror(errno));
        return false;
    }

    // TODO: Check for return values on all of the dprintf calls
    dprintf(file, "[%s]\n", m_name.c_str());
    dprintf(file, "type=listener\n");
    dprintf(file, "protocol=%s\n", m_protocol.c_str());
    dprintf(file, "service=%s\n", m_service->name);
    dprintf(file, "address=%s\n", m_address.c_str());
    dprintf(file, "port=%u\n", m_port);
    dprintf(file, "authenticator=%s\n", m_authenticator.c_str());

    if (!m_auth_options.empty())
    {
        dprintf(file, "authenticator_options=%s\n", m_auth_options.c_str());
    }

    if (m_ssl)
    {
        write_ssl_config(file, m_ssl);
    }

    ::close(file);

    return true;
}

bool listener_serialize(const SListener& listener)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename,
             sizeof(filename),
             "%s/%s.cnf.tmp",
             get_config_persistdir(),
             listener->name());

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary listener configuration at '%s': %d, %s",
                  filename,
                  errno,
                  mxs_strerror(errno));
    }
    else if (listener->create_listener_config(filename))
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

json_t* Listener::to_json() const
{
    json_t* param = json_object();
    json_object_set_new(param, "address", json_string(m_address.c_str()));
    json_object_set_new(param, "port", json_integer(m_port));
    json_object_set_new(param, "protocol", json_string(m_protocol.c_str()));
    json_object_set_new(param, "authenticator", json_string(m_authenticator.c_str()));
    json_object_set_new(param, "auth_options", json_string(m_auth_options.c_str()));

    if (m_ssl)
    {
        json_t* ssl = json_object();

        const char* ssl_method = ssl_method_type_to_string(m_ssl->ssl_method_type);
        json_object_set_new(ssl, "ssl_version", json_string(ssl_method));
        json_object_set_new(ssl, "ssl_cert", json_string(m_ssl->ssl_cert));
        json_object_set_new(ssl, "ssl_ca_cert", json_string(m_ssl->ssl_ca_cert));
        json_object_set_new(ssl, "ssl_key", json_string(m_ssl->ssl_key));

        json_object_set_new(param, "ssl", ssl);
    }

    json_t* attr = json_object();
    json_object_set_new(attr, CN_STATE, json_string(state()));
    json_object_set_new(attr, CN_PARAMETERS, param);

    if (m_auth_func.diagnostic_json)
    {
        json_t* diag = m_auth_func.diagnostic_json(this);

        if (diag)
        {
            json_object_set_new(attr, CN_AUTHENTICATOR_DIAGNOSTICS, diag);
        }
    }

    json_t* rval = json_object();
    json_object_set_new(rval, CN_ID, json_string(m_name.c_str()));
    json_object_set_new(rval, CN_TYPE, json_string(CN_LISTENERS));
    json_object_set_new(rval, CN_ATTRIBUTES, attr);

    return rval;
}

const char* Listener::name() const
{
    return m_name.c_str();
}

const char* Listener::address() const
{
    return m_address.c_str();
}

uint16_t Listener::port() const
{
    return m_port;
}

SERVICE* Listener::service() const
{
    return m_service;
}

const char* Listener::authenticator() const
{
    return m_authenticator.c_str();
}

const char* Listener::protocol() const
{
    return m_protocol.c_str();
}

const MXS_PROTOCOL& Listener::protocol_func() const
{
    return m_proto_func;
}

const MXS_AUTHENTICATOR& Listener::auth_func() const
{
    return m_auth_func;
}

void* Listener::auth_instance() const
{
    return m_auth_instance;
}

SSL_LISTENER* Listener::ssl() const
{
    return m_ssl;
}

const char* Listener::state() const
{
    switch (m_state)
    {
        case CREATED:
            return "Created";

        case STARTED:
            return "Running";

        case STOPPED:
            return "Stopped";

        case FAILED:
            return "Failed";

        case DESTROYED:
            return "Destroyed";

        default:
            mxb_assert(!true);
            return "Unknown";
    }
}

void Listener::print_users(DCB* dcb)
{
    if (m_auth_func.diagnostic)
    {
        dcb_printf(dcb, "User names (%s): ", name());

        m_auth_func.diagnostic(dcb, this);

        dcb_printf(dcb, "\n");
    }
}

int Listener::load_users()
{
    int rval = MXS_AUTH_LOADUSERS_OK;

    if (m_auth_func.loadusers)
    {
        rval = m_auth_func.loadusers(this);
    }

    return rval;
}

struct users* Listener::users() const
{
    return m_users;
}


void Listener::set_users(struct users* u)
{
    m_users = u;
}

namespace
{

/**
 * @brief Create a Unix domain socket
 *
 * @param path The socket path
 * @return     The opened socket or -1 on error
 */
static int create_unix_socket(const char* path)
{
    if (unlink(path) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to unlink Unix Socket %s: %d %s", path, errno, mxs_strerror(errno));
    }

    struct sockaddr_un local_addr;
    int listener_socket = open_unix_socket(MXS_SOCKET_LISTENER, &local_addr, path);

    if (listener_socket >= 0 && chmod(path, 0777) < 0)
    {
        MXS_ERROR("Failed to change permissions on UNIX Domain socket '%s': %d, %s",
                  path, errno, mxs_strerror(errno));
    }

    return listener_socket;
}

/**
 * @brief Create a listener, add new information to the given DCB
 *
 * First creates and opens a socket, either TCP or Unix according to the
 * configuration data provided.  Then try to listen on the socket and
 * record the socket in the given DCB.  Add the given DCB into the poll
 * list.  The protocol name does not affect the logic, but is used in
 * log messages.
 *
 * @param config Configuration for port to listen on
 *
 * @return New socket or -1 on error
 */
int start_listening(const char* config)
{
    char host[strlen(config) + 1];
    strcpy(host, config);
    char* port_str = strrchr(host, '|');
    uint16_t port = 0;

    if (port_str)
    {
        *port_str++ = 0;
        port = atoi(port_str);
    }

    mxb_assert(strchr(host, '/') || port > 0);

    int listener_socket = -1;

    if (strchr(host, '/'))
    {
        listener_socket = create_unix_socket(host);
    }
    else if (port > 0)
    {
        struct sockaddr_storage server_address = {};
        listener_socket = open_network_socket(MXS_SOCKET_LISTENER, &server_address, host, port);

        if (listener_socket == -1 && strcmp(host, "::") == 0)
        {
            /** Attempt to bind to the IPv4 if the default IPv6 one is used */
            MXS_WARNING("Failed to bind on default IPv6 host '::', attempting "
                        "to bind on IPv4 version '0.0.0.0'");
            strcpy(host, "0.0.0.0");
            listener_socket = open_network_socket(MXS_SOCKET_LISTENER, &server_address, host, port);
        }
    }

    if (listener_socket != -1)
    {
        /**
         * The use of INT_MAX for backlog length in listen() allows the end-user to
         * control the backlog length with the net.ipv4.tcp_max_syn_backlog kernel
         * option since the parameter is silently truncated to the configured value.
         *
         * @see man 2 listen
         */
        if (listen(listener_socket, INT_MAX) != 0)
        {
            MXS_ERROR("Failed to start listening on [%s]:%u: %d, %s", host, port, errno, mxs_strerror(errno));
            close(listener_socket);
            return -1;
        }
    }

    return listener_socket;
}


/**
 * @brief Accept a new client connection
 *
 * @param fd          File descriptor to accept from
 * @param client_conn Output where connection information is stored
 *
 * @return -1 for failure, or a file descriptor for the new connection
 */
static int accept_one_connection(int fd, struct sockaddr* client_conn)
{
    socklen_t client_len = sizeof(struct sockaddr_storage);

    /* new connection from client */
    int client_fd = accept(fd, client_conn, &client_len);

    if (client_fd == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
    {
        MXS_ERROR("Failed to accept new client connection: %d, %s", errno, mxs_strerror(errno));
    }

    return client_fd;
}

}

DCB* Listener::accept_one_dcb()
{
    DCB* client_dcb = NULL;
    int c_sock;
    struct sockaddr_storage client_conn;

    if ((c_sock = accept_one_connection(fd(), (struct sockaddr*)&client_conn)) >= 0)
    {
        configure_network_socket(c_sock, client_conn.ss_family);

        client_dcb = dcb_alloc(DCB_ROLE_CLIENT_HANDLER, m_self, m_service);

        if (client_dcb == NULL)
        {
            MXS_ERROR("Failed to create DCB object for client connection.");
            close(c_sock);
        }
        else
        {
            client_dcb->fd = c_sock;

            // get client address
            if (client_conn.ss_family == AF_UNIX)
            {
                // client address
                client_dcb->ip.ss_family = AF_UNIX;
                client_dcb->remote = MXS_STRDUP_A("localhost");
            }
            else
            {
                /* client IP in raw data*/
                memcpy(&client_dcb->ip, &client_conn, sizeof(client_conn));
                /* client IP in string representation */
                client_dcb->remote = (char*)MXS_CALLOC(INET6_ADDRSTRLEN + 1, sizeof(char));

                if (client_dcb->remote)
                {
                    void* ptr;
                    if (client_dcb->ip.ss_family == AF_INET)
                    {
                        ptr = &((struct sockaddr_in*)&client_dcb->ip)->sin_addr;
                    }
                    else
                    {
                        ptr = &((struct sockaddr_in6*)&client_dcb->ip)->sin6_addr;
                    }

                    inet_ntop(client_dcb->ip.ss_family, ptr, client_dcb->remote, INET6_ADDRSTRLEN);
                }
            }

            /** Allocate DCB specific authentication data */
            if (client_dcb->authfunc.create
                && (client_dcb->authenticator_data =
                        client_dcb->authfunc.create(client_dcb->listener->auth_instance())) == NULL)
            {
                MXS_ERROR("Failed to create authenticator for client DCB");
                dcb_close(client_dcb);
                return NULL;
            }

            if (client_dcb->service->max_connections
                && client_dcb->service->client_count >= client_dcb->service->max_connections)
            {
                // TODO: If connections can be queued, this is the place to put the
                // TODO: connection on that queue.
                if (client_dcb->func.connlimit)
                {
                    client_dcb->func.connlimit(client_dcb, client_dcb->service->max_connections);
                }
                client_dcb->session->close_reason = SESSION_CLOSE_TOO_MANY_CONNECTIONS;
                dcb_close(client_dcb);
                client_dcb = NULL;
            }
        }
    }

    if (client_dcb)
    {
        mxb::atomic::add(&m_service->client_count, 1);
    }

    return client_dcb;
}

bool Listener::listen_shared(std::string config_bind)
{
    bool rval = false;
    int fd = start_listening(config_bind.data());

    if (fd != -1)
    {
        if (mxs::RoutingWorker::add_shared_fd(fd, EPOLLIN, this))
        {
            m_fd = fd;
            rval = true;
            m_state = STARTED;
        }
        else
        {
            close(fd);
        }
    }
    else
    {
        MXS_ERROR("[%s] Failed to listen on %s", m_service->name, config_bind.c_str());
    }

    return rval;
}

bool Listener::listen()
{
    m_state = FAILED;

    /** Load the authentication users before before starting the listener */
    if (m_auth_func.loadusers)
    {
        switch (m_auth_func.loadusers(this))
        {
        case MXS_AUTH_LOADUSERS_FATAL:
            MXS_ERROR("[%s] Fatal error when loading users for listener '%s', "
                      "service is not started.", m_service->name, name());
            return 0;

        case MXS_AUTH_LOADUSERS_ERROR:
            MXS_WARNING("[%s] Failed to load users for listener '%s', authentication"
                        " might not work.", m_service->name, name());
            break;

        default:
            break;
        }
    }

    bool rval = false;
    std::stringstream ss;
    ss << m_address << "|" << m_port;

    // TODO: Detect the need for SO_REUSEPORT here
    rval = listen_shared(ss.str());

    if (rval)
    {
        MXS_NOTICE("Listening for connections at [%s]:%u", m_address.c_str(), m_port);
    }

    return rval;
}

uint32_t Listener::poll_handler(MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events)
{
    Listener* listener = static_cast<Listener*>(data);
    DCB* client_dcb;

    while ((client_dcb = listener->accept_one_dcb()))
    {
        listener->m_proto_func.accept(client_dcb);
    }

    return 1;
}
