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

#include <maxscale/cppdefs.hh>

#include <climits>
#include <new>
#include <microhttpd.h>

#include <maxscale/atomic.h>
#include <maxscale/debug.h>
#include <maxscale/thread.h>
#include <maxscale/utils.h>
#include <maxscale/config.h>
#include <maxscale/hk_heartbeat.h>

#include "maxscale/admin.hh"
#include "maxscale/resource.hh"

static struct MHD_Daemon* http_daemon = NULL;

int handle_client(void *cls,
                  struct MHD_Connection *connection,
                  const char *url,
                  const char *method,
                  const char *version,
                  const char *upload_data,
                  size_t *upload_data_size,
                  void **con_cls)

{
    const char *admin_user = config_get_global_options()->admin_user;
    const char *admin_pw = config_get_global_options()->admin_password;
    bool admin_auth = config_get_global_options()->admin_auth;

    char* pw = NULL;
    char* user = MHD_basic_auth_get_username_password(connection, &pw);

    if (admin_auth && (!user || !pw || strcmp(user, admin_user) || strcmp(pw, admin_pw)))
    {
        static char error_resp[] = "Access denied\r\n";
        struct MHD_Response *resp;

        resp = MHD_create_response_from_buffer(sizeof (error_resp) - 1, error_resp,
                                               MHD_RESPMEM_PERSISTENT);

        MHD_queue_basic_auth_fail_response(connection, "maxscale", resp);
        MHD_destroy_response(resp);
        return MHD_YES;
    }

    string verb(method);
    json_t* json = NULL;

    if (verb == "POST" || verb == "PUT" || verb == "PATCH")
    {
        json_error_t err = {};
        if ((json = json_loadb(upload_data, *upload_data_size, 0, &err)) == NULL)
        {
            return MHD_NO;
        }
    }

    HttpRequest request(connection, url, method, json);
    HttpResponse reply = resource_handle_request(request);

    string data = reply.get_response();

    struct MHD_Response *response = MHD_create_response_from_buffer(data.size(),
                                                                    (void*)data.c_str(),
                                                                    MHD_RESPMEM_MUST_COPY);

    for (map<string, string>::const_iterator it = reply.get_headers().begin();
         it != reply.get_headers().end(); it++)
    {
        MHD_add_response_header(response, it->first.c_str(), it->second.c_str());
    }

    MHD_queue_response(connection, reply.get_code(), response);
    MHD_destroy_response(response);
    return MHD_YES;
}

bool mxs_admin_init()
{
    http_daemon = MHD_start_daemon(MHD_USE_EPOLL_INTERNALLY | MHD_USE_DUAL_STACK,
                                   config_get_global_options()->admin_port,
                                   NULL, NULL,
                                   handle_client, NULL,
                                   MHD_OPTION_END);
    return http_daemon != NULL;

}

void mxs_admin_shutdown()
{
    MHD_stop_daemon(http_daemon);
}
