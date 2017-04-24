/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/admin.hh"

#include <climits>
#include <new>
#include <microhttpd.h>

#include <maxscale/atomic.h>
#include <maxscale/debug.h>
#include <maxscale/thread.h>
#include <maxscale/utils.h>
#include <maxscale/config.h>
#include <maxscale/hk_heartbeat.h>

#include "maxscale/resource.hh"
#include "maxscale/http.hh"

using std::string;

static struct MHD_Daemon* http_daemon = NULL;

int kv_iter(void *cls,
            enum MHD_ValueKind kind,
            const char *key,
            const char *value)
{
    size_t* rval = (size_t*)cls;

    if (strcmp(key, "Content-Length") == 0)
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
            method == MHD_HTTP_METHOD_DELETE) &&
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
    HttpResponse reply = resource_handle_request(request);

    string data;

    json_t* js = reply.get_response();

    if (js)
    {
        int flags = request.get_option("pretty") == "true" ? JSON_INDENT(4) : 0;
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

bool do_auth(MHD_Connection *connection)
{
    const char *admin_user = config_get_global_options()->admin_user;
    const char *admin_pw = config_get_global_options()->admin_password;
    bool admin_auth = config_get_global_options()->admin_auth;

    char* pw = NULL;
    char* user = MHD_basic_auth_get_username_password(connection, &pw);
    bool rval = true;

    if (admin_auth && (!user || !pw || strcmp(user, admin_user) || strcmp(pw, admin_pw)))
    {
        rval = false;
        static char error_resp[] = "Access denied\r\n";
        MHD_Response *resp =
            MHD_create_response_from_buffer(sizeof(error_resp) - 1, error_resp,
                                            MHD_RESPMEM_PERSISTENT);

        MHD_queue_basic_auth_fail_response(connection, "maxscale", resp);
        MHD_destroy_response(resp);
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
    if (!do_auth(connection))
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

bool mxs_admin_init()
{
    http_daemon = MHD_start_daemon(MHD_USE_EPOLL_INTERNALLY_LINUX_ONLY | MHD_USE_DUAL_STACK,
                                   config_get_global_options()->admin_port,
                                   NULL, NULL,
                                   handle_client, NULL,
                                   MHD_OPTION_NOTIFY_COMPLETED, close_client, NULL,
                                   MHD_OPTION_END);
    return http_daemon != NULL;

}

void mxs_admin_shutdown()
{
    MHD_stop_daemon(http_daemon);
}
