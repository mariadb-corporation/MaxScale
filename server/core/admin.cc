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
 * @file The embedded HTTP protocol administrative interface
 */
#include "maxscale/admin.hh"

#include <climits>
#include <new>
#include <fstream>
#include <microhttpd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/stat.h>

#include <maxscale/atomic.h>
#include <maxscale/debug.h>
#include <maxscale/thread.h>
#include <maxscale/utils.h>
#include <maxscale/config.h>
#include <maxscale/hk_heartbeat.h>
#include <maxscale/http.hh>
#include <maxscale/adminusers.h>

#include "maxscale/resource.hh"

using std::string;
using std::ifstream;

static struct MHD_Daemon* http_daemon = NULL;

/** In-memory certificates in PEM format */
static char* admin_ssl_key     = NULL;
static char* admin_ssl_cert    = NULL;
static char* admin_ssl_ca_cert = NULL;

static bool  using_ssl = false;

int kv_iter(void *cls,
            enum MHD_ValueKind kind,
            const char *key,
            const char *value)
{
    size_t* rval = (size_t*)cls;

    if (strcasecmp(key, "Content-Length") == 0)
    {
        *rval = atoi(value);
        return MHD_NO;
    }

    return MHD_YES;
}

static inline size_t request_data_length(MHD_Connection *connection)
{
    size_t rval = 0;
    MHD_get_connection_values(connection, MHD_HEADER_KIND, kv_iter, &rval);
    return rval;
}

static bool modifies_data(MHD_Connection *connection, string method)
{
    return (method == MHD_HTTP_METHOD_POST || method == MHD_HTTP_METHOD_PUT ||
            method == MHD_HTTP_METHOD_DELETE || method == MHD_HTTP_METHOD_PATCH) &&
           request_data_length(connection);
}

int Client::process(string url, string method, const char* upload_data, size_t *upload_size)
{
    json_t* json = NULL;

    if (*upload_size)
    {
        m_data.append(upload_data, *upload_size);
        *upload_size = 0;
        return MHD_YES;
    }

    json_error_t err = {};

    if (m_data.length() &&
        (json = json_loadb(m_data.c_str(), m_data.size(), 0, &err)) == NULL)
    {
        MHD_Response *response =
            MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
        MHD_queue_response(m_connection, MHD_HTTP_BAD_REQUEST, response);
        MHD_destroy_response(response);
        return MHD_YES;
    }

    HttpRequest request(m_connection, url, method, json);
    HttpResponse reply(MHD_HTTP_NOT_FOUND);

    MXS_DEBUG("Request:\n%s", request.to_string().c_str());

    if (url == "/")
    {
        // Respond to pings with 200 OK
        reply = HttpResponse(MHD_HTTP_OK);
    }
    else if (request.validate_api_version())
    {
        reply = resource_handle_request(request);
    }

    string data;

    json_t* js = reply.get_response();

    if (js)
    {
        int flags = 0;
        string pretty = request.get_option("pretty");

        if (pretty == "true" || pretty.length() == 0)
        {
            flags |= JSON_INDENT(4);
        }

        data = mxs::json_dump(js, flags);
    }

    MHD_Response *response =
        MHD_create_response_from_buffer(data.size(), (void*)data.c_str(),
                                        MHD_RESPMEM_MUST_COPY);

    const Headers& headers = reply.get_headers();

    for (Headers::const_iterator it = headers.begin(); it != headers.end(); it++)
    {
        MHD_add_response_header(response, it->first.c_str(), it->second.c_str());
    }

    int rval = MHD_queue_response(m_connection, reply.get_code(), response);
    MHD_destroy_response(response);

    return rval;
}

void close_client(void *cls,
                  MHD_Connection *connection,
                  void **con_cls,
                  enum MHD_RequestTerminationCode toe)
{
    Client* client = static_cast<Client*>(*con_cls);
    delete client;
}

bool do_auth(MHD_Connection *connection, const char* url)
{
    bool rval = true;

    if (config_get_global_options()->admin_auth)
    {
        char* pw = NULL;
        char* user = MHD_basic_auth_get_username_password(connection, &pw);

        if (!user || !pw || !admin_verify_inet_user(user, pw))
        {
            if (config_get_global_options()->admin_log_auth_failures)
            {
                MXS_WARNING("Authentication failed for '%s', %s. Request: %s", user ? user : "",
                            pw ? "using password" : "no password", url);
            }
            rval = false;
            static char error_resp[] = "{\"errors\": [ { \"detail\": \"Access denied\" } ] }";
            MHD_Response *resp =
                MHD_create_response_from_buffer(sizeof(error_resp) - 1, error_resp,
                                                MHD_RESPMEM_PERSISTENT);

            MHD_queue_basic_auth_fail_response(connection, "maxscale", resp);
            MHD_destroy_response(resp);
        }
        else
        {
            MXS_INFO("Accept authentication from '%s', %s. Request: %s", user ? user : "",
                     pw ? "using password" : "no password", url);
        }
        MXS_FREE(user);
        MXS_FREE(pw);
    }

    return rval;
}

int handle_client(void *cls,
                  MHD_Connection *connection,
                  const char *url,
                  const char *method,
                  const char *version,
                  const char *upload_data,
                  size_t *upload_data_size,
                  void **con_cls)

{
    if (!do_auth(connection, url))
    {
        return MHD_YES;
    }

    if (*con_cls == NULL)
    {
        if ((*con_cls = new (std::nothrow) Client(connection)) == NULL)
        {
            return MHD_NO;
        }
        else if (modifies_data(connection, method))
        {
            // The first call doesn't have any data
            return MHD_YES;
        }
    }

    Client* client = static_cast<Client*>(*con_cls);
    return client->process(url, method, upload_data, upload_data_size);
}

static bool host_to_sockaddr(const char* host, uint16_t port, struct sockaddr_storage* addr)
{
    struct addrinfo *ai = NULL, hint = {};
    int rc;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_family = AF_UNSPEC;
    hint.ai_flags = AI_ALL;

    if ((rc = getaddrinfo(host, NULL, &hint, &ai)) != 0)
    {
        MXS_ERROR("Failed to obtain address for host %s: %s", host, gai_strerror(rc));
        return false;
    }

    /* Take the first one */
    if (ai)
    {
        memcpy(addr, ai->ai_addr, ai->ai_addrlen);

        if (addr->ss_family == AF_INET)
        {
            struct sockaddr_in *ip = (struct sockaddr_in*)addr;
            (*ip).sin_port = htons(port);
        }
        else if (addr->ss_family == AF_INET6)
        {
            struct sockaddr_in6 *ip = (struct sockaddr_in6*)addr;
            (*ip).sin6_port = htons(port);
        }
    }

    freeaddrinfo(ai);
    return true;
}

static char* load_cert(const char* file)
{
    char* rval = NULL;
    ifstream infile(file);
    struct stat st;

    if (stat(file, &st) == 0 &&
        (rval = new (std::nothrow) char[st.st_size + 1]))
    {
        infile.read(rval, st.st_size);
        rval[st.st_size] = '\0';

        if (!infile.good())
        {
            MXS_ERROR("Failed to load certificate file: %s", file);
            delete rval;
            rval = NULL;
        }
    }

    return rval;
}

static bool load_ssl_certificates()
{
    bool rval = false;
    const char* key = config_get_global_options()->admin_ssl_key;
    const char* cert = config_get_global_options()->admin_ssl_cert;
    const char* ca = config_get_global_options()->admin_ssl_ca_cert;

    if (*key && *cert && *ca)
    {
        if ((admin_ssl_key = load_cert(key)) &&
            (admin_ssl_cert = load_cert(cert)) &&
            (admin_ssl_ca_cert = load_cert(ca)))
        {
            rval = true;
        }
        else
        {
            delete admin_ssl_key;
            delete admin_ssl_cert;
            delete admin_ssl_ca_cert;
            admin_ssl_key = NULL;
            admin_ssl_cert = NULL;
            admin_ssl_ca_cert = NULL;
        }
    }

    return rval;
}

bool mxs_admin_init()
{
    struct sockaddr_storage addr;

    if (host_to_sockaddr(config_get_global_options()->admin_host,
                         config_get_global_options()->admin_port,
                         &addr))
    {
        int options = MHD_USE_EPOLL_INTERNALLY_LINUX_ONLY;

        if (addr.ss_family == AF_INET6)
        {
            options |= MHD_USE_DUAL_STACK;
        }

        if (load_ssl_certificates())
        {
            using_ssl = true;
            options |= MHD_USE_SSL;
        }

        // The port argument is ignored and the port in the struct sockaddr is used instead
        http_daemon = MHD_start_daemon(options, 0, NULL, NULL, handle_client, NULL,
                                       MHD_OPTION_NOTIFY_COMPLETED, close_client, NULL,
                                       MHD_OPTION_SOCK_ADDR, &addr,
                                       !using_ssl ? MHD_OPTION_END :
                                       MHD_OPTION_HTTPS_MEM_KEY, admin_ssl_key,
                                       MHD_OPTION_HTTPS_MEM_CERT, admin_ssl_cert,
                                       MHD_OPTION_HTTPS_MEM_TRUST, admin_ssl_cert,
                                       MHD_OPTION_END);
    }

    return http_daemon != NULL;
}

void mxs_admin_shutdown()
{
    MHD_stop_daemon(http_daemon);
}

bool mxs_admin_https_enabled()
{
    return using_ssl;
}
