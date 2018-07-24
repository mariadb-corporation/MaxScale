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
#include "internal/resource.hh"

#include <list>
#include <sstream>
#include <map>

#include <maxscale/alloc.h>
#include <maxscale/jansson.hh>
#include <maxscale/spinlock.hh>
#include <maxscale/json_api.h>
#include <maxscale/housekeeper.h>
#include <maxscale/http.hh>
#include <maxscale/adminusers.h>
#include <maxscale/modulecmd.h>
#include <maxscale/semaphore.hh>
#include <maxscale/server.hh>

#include "internal/httprequest.hh"
#include "internal/httpresponse.hh"
#include "internal/session.h"
#include "internal/filter.h"
#include "internal/monitor.h"
#include "internal/service.h"
#include "internal/config_runtime.h"
#include "internal/modules.h"
#include "internal/routingworker.hh"

using std::list;
using std::map;
using std::string;
using std::stringstream;
using mxs::SpinLock;
using mxs::SpinLockGuard;

Resource::Resource(ResourceCallback cb, int components, ...) :
    m_cb(cb),
    m_is_glob(false),
    m_constraints(NONE)
{
    va_list args;
    va_start(args, components);

    for (int i = 0; i < components; i++)
    {
        string part = va_arg(args, const char*);
        m_path.push_back(part);
        if (part == "?")
        {
            m_is_glob = true;
        }
    }
    va_end(args);
}

Resource::~Resource()
{
}

bool Resource::match(const HttpRequest& request) const
{
    bool rval = false;

    if (request.uri_part_count() == m_path.size() || m_is_glob)
    {
        rval = true;
        size_t parts = MXS_MIN(request.uri_part_count(), m_path.size());

        for (size_t i = 0; i < parts; i++)
        {
            if (m_path[i] != request.uri_part(i) &&
                !matching_variable_path(m_path[i], request.uri_part(i)))
            {
                rval = false;
                break;
            }
        }
    }

    return rval;
}

HttpResponse Resource::call(const HttpRequest& request) const
{
    return m_cb(request);
};

bool Resource::matching_variable_path(const string& path, const string& target) const
{
    bool rval = false;

    if (path[0] == ':')
    {
        if ((path == ":service" && service_find(target.c_str())) ||
            (path == ":server" && server_find_by_unique_name(target.c_str())) ||
            (path == ":filter" && filter_def_find(target.c_str())) ||
            (path == ":monitor" && monitor_find(target.c_str())) ||
            (path == ":module" && get_module(target.c_str(), NULL)) ||
            (path == ":inetuser" && admin_inet_user_exists(target.c_str())) ||
            (path == ":unixuser" && admin_linux_account_enabled(target.c_str())))
        {
            rval = true;
        }
        else if (path == ":session")
        {
            size_t id = atoi(target.c_str());
            MXS_SESSION* ses = session_get_by_id(id);

            if (ses)
            {
                session_put_ref(ses);
                rval = true;
            }
        }
        else if (path == ":thread")
        {
            char* end;
            int id = strtol(target.c_str(), &end, 10);

            if (*end == '\0' && mxs_rworker_get(id))
            {
                rval = true;
            }
        }
    }
    else if (path == "?")
    {
        /** Wildcard match */
        rval = true;
    }

    return rval;
}

void Resource::add_constraint(resource_constraint type)
{
    m_constraints |= static_cast<uint32_t>(type);
}

bool Resource::requires_body() const
{
    return m_constraints & REQUIRE_BODY;
}

namespace
{

static bool drop_path_part(std::string& path)
{
    size_t pos = path.find_last_of('/');
    bool rval = false;

    if (pos != std::string::npos)
    {
        path.erase(pos);
        rval = true;
    }

    return rval && path.length();
}

/**
 * Class that keeps track of resource modification times
 */
class ResourceWatcher
{
public:

    ResourceWatcher() :
        m_init(time(NULL))
    {
    }

    void modify(const std::string& orig_path)
    {
        std::string path = orig_path;

        do
        {
            map<std::string, uint64_t>::iterator it = m_etag.find(path);

            if (it != m_etag.end())
            {
                it->second++;
            }
            else
            {
                // First modification
                m_etag[path] = 1;
            }

            m_last_modified[path] = time(NULL);
        }
        while (drop_path_part(path));
    }

    time_t last_modified(const string& path) const
    {
        map<string, time_t>::const_iterator it = m_last_modified.find(path);

        if (it != m_last_modified.end())
        {
            return it->second;
        }

        // Resource has not yet been updated
        return m_init;
    }

    uint64_t etag(const string& path) const
    {
        map<string, uint64_t>::const_iterator it = m_etag.find(path);

        if (it != m_etag.end())
        {
            return it->second;
        }

        // Resource has not yet been updated
        return 0;
    }

private:
    time_t m_init;
    map<string, time_t> m_last_modified;
    map<string, uint64_t> m_etag;
};

HttpResponse cb_stop_monitor(const HttpRequest& request)
{
    MXS_MONITOR* monitor = monitor_find(request.uri_part(1).c_str());
    monitor_stop(monitor);
    return HttpResponse(MHD_HTTP_NO_CONTENT);
}

HttpResponse cb_start_monitor(const HttpRequest& request)
{
    MXS_MONITOR* monitor = monitor_find(request.uri_part(1).c_str());
    monitor_start(monitor, monitor->parameters);
    return HttpResponse(MHD_HTTP_NO_CONTENT);
}

HttpResponse cb_stop_service(const HttpRequest& request)
{
    SERVICE* service = service_find(request.uri_part(1).c_str());
    serviceStop(service);
    return HttpResponse(MHD_HTTP_NO_CONTENT);
}

HttpResponse cb_start_service(const HttpRequest& request)
{
    SERVICE* service = service_find(request.uri_part(1).c_str());
    serviceStart(service);
    return HttpResponse(MHD_HTTP_NO_CONTENT);
}

HttpResponse cb_create_server(const HttpRequest& request)
{
    ss_dassert(request.get_json());

    if (runtime_create_server_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_server(const HttpRequest& request)
{
    SERVER* server = server_find_by_unique_name(request.uri_part(1).c_str());
    ss_dassert(server && request.get_json());

    if (runtime_alter_server_from_json(server, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse do_alter_server_relationship(const HttpRequest& request, const char* type)
{
    SERVER* server = server_find_by_unique_name(request.uri_part(1).c_str());
    ss_dassert(server && request.get_json());

    if (runtime_alter_server_relationships_from_json(server, type, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_server_service_relationship(const HttpRequest& request)
{
    return do_alter_server_relationship(request, "services");
}

HttpResponse cb_alter_server_monitor_relationship(const HttpRequest& request)
{
    return do_alter_server_relationship(request, "monitors");
}

HttpResponse cb_create_monitor(const HttpRequest& request)
{
    ss_dassert(request.get_json());

    if (runtime_create_monitor_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_create_filter(const HttpRequest& request)
{
    ss_dassert(request.get_json());

    if (runtime_create_filter_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_create_service(const HttpRequest& request)
{
    ss_dassert(request.get_json());

    if (runtime_create_service_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_create_service_listener(const HttpRequest& request)
{
    SERVICE* service = service_find(request.uri_part(1).c_str());
    ss_dassert(service && request.get_json());

    if (runtime_create_listener_from_json(service, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_monitor(const HttpRequest& request)
{
    MXS_MONITOR* monitor = monitor_find(request.uri_part(1).c_str());
    ss_dassert(monitor && request.get_json());

    if (runtime_alter_monitor_from_json(monitor, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_monitor_server_relationship(const HttpRequest& request)
{
    MXS_MONITOR* monitor = monitor_find(request.uri_part(1).c_str());
    ss_dassert(monitor && request.get_json());

    if (runtime_alter_monitor_relationships_from_json(monitor, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_service(const HttpRequest& request)
{
    SERVICE* service = service_find(request.uri_part(1).c_str());
    ss_dassert(service && request.get_json());

    if (runtime_alter_service_from_json(service, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_service_server_relationship(const HttpRequest& request)
{
    SERVICE* service = service_find(request.uri_part(1).c_str());
    ss_dassert(service && request.get_json());

    if (runtime_alter_service_relationships_from_json(service, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_logs(const HttpRequest& request)
{
    ss_dassert(request.get_json());

    if (runtime_alter_logs_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_delete_server(const HttpRequest& request)
{
    SERVER* server = server_find_by_unique_name(request.uri_part(1).c_str());
    ss_dassert(server);

    if (runtime_destroy_server(server))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_delete_monitor(const HttpRequest& request)
{
    MXS_MONITOR* monitor = monitor_find(request.uri_part(1).c_str());
    ss_dassert(monitor);

    if (runtime_destroy_monitor(monitor))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_delete_listener(const HttpRequest& request)
{

    SERVICE* service = service_find(request.uri_part(1).c_str());
    ss_dassert(service);
    std::string listener = request.uri_part(3);

    if (!service_has_named_listener(service, listener.c_str()))
    {
        return HttpResponse(MHD_HTTP_NOT_FOUND);
    }
    else if (!runtime_destroy_listener(service, listener.c_str()))
    {
        return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
    }

    return HttpResponse(MHD_HTTP_NO_CONTENT);
}

HttpResponse cb_delete_service(const HttpRequest& request)
{
    SERVICE* service = service_find(request.uri_part(1).c_str());
    ss_dassert(service);

    if (runtime_destroy_service(service))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_delete_filter(const HttpRequest& request)
{
    MXS_FILTER_DEF* filter = filter_def_find(request.uri_part(1).c_str());
    ss_dassert(filter);

    if (runtime_destroy_filter(filter))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}
HttpResponse cb_all_servers(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, server_list_to_json(request.host()));
}

HttpResponse cb_get_server(const HttpRequest& request)
{
    SERVER* server = server_find_by_unique_name(request.uri_part(1).c_str());
    ss_dassert(server);
    return HttpResponse(MHD_HTTP_OK, server_to_json(server, request.host()));
}

HttpResponse cb_all_services(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, service_list_to_json(request.host()));
}

HttpResponse cb_get_service(const HttpRequest& request)
{
    SERVICE* service = service_find(request.uri_part(1).c_str());
    ss_dassert(service);
    return HttpResponse(MHD_HTTP_OK, service_to_json(service, request.host()));
}

HttpResponse cb_get_all_service_listeners(const HttpRequest& request)
{
    SERVICE* service = service_find(request.uri_part(1).c_str());
    return HttpResponse(MHD_HTTP_OK, service_listener_list_to_json(service, request.host()));
}

HttpResponse cb_get_service_listener(const HttpRequest& request)
{
    SERVICE* service = service_find(request.uri_part(1).c_str());
    std::string listener = request.uri_part(3);
    ss_dassert(service);

    if (!service_has_named_listener(service, listener.c_str()))
    {
        return HttpResponse(MHD_HTTP_NOT_FOUND);
    }

    return HttpResponse(MHD_HTTP_OK,
                        service_listener_to_json(service, listener.c_str(),
                                                 request.host()));
}

HttpResponse cb_all_filters(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, filter_list_to_json(request.host()));
}

HttpResponse cb_get_filter(const HttpRequest& request)
{
    MXS_FILTER_DEF* filter = filter_def_find(request.uri_part(1).c_str());
    ss_dassert(filter);
    return HttpResponse(MHD_HTTP_OK, filter_to_json(filter, request.host()));
}

HttpResponse cb_all_monitors(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, monitor_list_to_json(request.host()));
}

HttpResponse cb_get_monitor(const HttpRequest& request)
{
    MXS_MONITOR* monitor = monitor_find(request.uri_part(1).c_str());
    ss_dassert(monitor);
    return HttpResponse(MHD_HTTP_OK, monitor_to_json(monitor, request.host()));
}

HttpResponse cb_all_sessions(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, session_list_to_json(request.host()));
}

HttpResponse cb_get_session(const HttpRequest& request)
{
    int id = atoi(request.uri_part(1).c_str());
    MXS_SESSION* session = session_get_by_id(id);

    if (session)
    {
        json_t* json = session_to_json(session, request.host());
        session_put_ref(session);
        return HttpResponse(MHD_HTTP_OK, json);
    }

    return HttpResponse(MHD_HTTP_NOT_FOUND);
}

HttpResponse cb_maxscale(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, config_maxscale_to_json(request.host()));
}

HttpResponse cb_alter_maxscale(const HttpRequest& request)
{
    ss_dassert(request.get_json());

    if (runtime_alter_maxscale_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_logs(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, mxs_logs_to_json(request.host()));
}

HttpResponse cb_flush(const HttpRequest& request)
{
    int code = MHD_HTTP_INTERNAL_SERVER_ERROR;

    // Flush logs
    if (mxs_log_rotate() == 0)
    {
        code = MHD_HTTP_NO_CONTENT;
    }

    return HttpResponse(code);
}

HttpResponse cb_all_threads(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, mxs_rworker_list_to_json(request.host()));
}

HttpResponse cb_thread(const HttpRequest& request)
{
    int id = atoi(request.last_uri_part().c_str());
    return HttpResponse(MHD_HTTP_OK, mxs_rworker_to_json(request.host(), id));
}

HttpResponse cb_tasks(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, hk_tasks_json(request.host()));
}

HttpResponse cb_all_modules(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, module_list_to_json(request.host()));
}

HttpResponse cb_module(const HttpRequest& request)
{
    const MXS_MODULE* module = get_module(request.last_uri_part().c_str(), NULL);
    return HttpResponse(MHD_HTTP_OK, module_to_json(module, request.host()));
}

HttpResponse cb_all_users(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, admin_all_users_to_json(request.host(), USER_TYPE_ALL));
}

HttpResponse cb_all_inet_users(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, admin_all_users_to_json(request.host(), USER_TYPE_INET));
}

HttpResponse cb_all_unix_users(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, admin_all_users_to_json(request.host(), USER_TYPE_UNIX));
}

HttpResponse cb_inet_user(const HttpRequest& request)
{
    string user = request.uri_part(2);
    return HttpResponse(MHD_HTTP_OK, admin_user_to_json(request.host(), user.c_str(), USER_TYPE_INET));
}

HttpResponse cb_unix_user(const HttpRequest& request)
{
    string user = request.uri_part(2);
    return HttpResponse(MHD_HTTP_OK, admin_user_to_json(request.host(), user.c_str(), USER_TYPE_UNIX));
}

HttpResponse cb_create_user(const HttpRequest& request)
{
    ss_dassert(request.get_json());

    if (runtime_create_user_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_delete_user(const HttpRequest& request)
{
    string user = request.last_uri_part();
    string type = request.uri_part(1);

    if ((type == CN_INET && runtime_remove_user(user.c_str(), USER_TYPE_INET)) ||
        (type == CN_UNIX && runtime_remove_user(user.c_str(), USER_TYPE_UNIX)))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_set_server(const HttpRequest& request)
{
    SERVER* server = server_find_by_unique_name(request.uri_part(1).c_str());
    int opt = server_map_status(request.get_option(CN_STATE).c_str());

    if (opt)
    {
        string errmsg;
        if (mxs::server_set_status(server, opt, &errmsg))
        {
            return HttpResponse(MHD_HTTP_NO_CONTENT);
        }
        else
        {
            return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error(errmsg.c_str()));
        }
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN,
                        mxs_json_error("Invalid or missing value for the `%s` "
                                       "parameter", CN_STATE));
}

HttpResponse cb_clear_server(const HttpRequest& request)
{
    SERVER* server = server_find_by_unique_name(request.uri_part(1).c_str());
    int opt = server_map_status(request.get_option(CN_STATE).c_str());

    if (opt)
    {
        string errmsg;
        if (mxs::server_clear_status(server, opt, &errmsg))
        {
            return HttpResponse(MHD_HTTP_NO_CONTENT);
        }
        else
        {
            return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error(errmsg.c_str()));
        }
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN,
                        mxs_json_error( "Invalid or missing value for the `%s` "
                                        "parameter", CN_STATE));
}

HttpResponse cb_modulecmd(const HttpRequest& request)
{
    std::string module = request.uri_part(2);
    std::string identifier = request.uri_segment(3, request.uri_part_count());
    std::string verb = request.get_verb();

    const MODULECMD* cmd = modulecmd_find_command(module.c_str(), identifier.c_str());

    if (cmd)
    {
        if ((!MODULECMD_MODIFIES_DATA(cmd) && verb == MHD_HTTP_METHOD_GET) ||
            (MODULECMD_MODIFIES_DATA(cmd) && verb == MHD_HTTP_METHOD_POST))
        {
            int n_opts = (int)request.get_option_count();
            char* opts[n_opts];
            request.copy_options(opts);

            MODULECMD_ARG* args = modulecmd_arg_parse(cmd, n_opts, (const void**)opts);
            bool rval = false;
            json_t* output = NULL;

            if (args)
            {
                rval = modulecmd_call_command(cmd, args, &output);
            }

            for (int i = 0; i < n_opts; i++)
            {
                MXS_FREE(opts[i]);
            }

            int rc;

            if (output && json_object_get(output, "errors") == NULL)
            {
                /**
                 * Store the command output in the meta field. This allows
                 * all the commands to conform to the JSON API even though
                 * the content of the field can vary from command to command.
                 *
                 * If the output is an JSON API error, we don't do anything to it
                 */
                std::string self = "/"; // The uri_segment doesn't have the leading slash
                self += request.uri_segment(0, request.uri_part_count());
                output = mxs_json_metadata(request.host(), self.c_str(), output);
            }

            if (rval)
            {
                rc = output ? MHD_HTTP_OK : MHD_HTTP_NO_CONTENT;
            }
            else
            {
                rc = MHD_HTTP_FORBIDDEN;
                json_t* err = modulecmd_get_json_error();

                if (err)
                {
                    if (!output)
                    {
                        // No output, only errors
                        output = err;
                    }
                    else
                    {
                        // Both output and errors
                        json_t* output_err = json_object_get(output, "errors");

                        if (output_err)
                        {
                            // The output already contains an error array and append to it
                            json_array_append(output_err, json_object_get(err, "errors"));
                        }
                        else
                        {
                            // No error, we can just assign the error array
                            json_object_set(output, "errors", json_object_get(err, "errors"));
                        }

                        json_decref(err);
                    }
                }
            }

            return HttpResponse(rc, output);
        }
    }

    return HttpResponse(MHD_HTTP_NOT_FOUND);
}

HttpResponse cb_send_ok(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK);
}

class RootResource
{
    RootResource(const RootResource&);
    RootResource& operator=(const RootResource&);
public:
    typedef std::shared_ptr<Resource> SResource;
    typedef list<SResource> ResourceList;

    /**
     * Create REST API resources
     *
     * Each resource represents either a collection of resources, an individual
     * resource, a sub-resource of a resource or an "action" endpoint which
     * executes an action.
     *
     * The resources are defined by the Resource class. Each resource maps to a
     * HTTP method and one or more paths. The path components can contain either
     * an explicit string, a colon-prefixed object type or a question mark for
     * a path component that matches everything.
     */
    RootResource()
    {
        // Special resources required by OPTION etc.
        m_get.push_back(SResource(new Resource(cb_send_ok, 0)));
        m_get.push_back(SResource(new Resource(cb_send_ok, 1, "*")));

        m_get.push_back(SResource(new Resource(cb_all_servers, 1, "servers")));
        m_get.push_back(SResource(new Resource(cb_get_server, 2, "servers", ":server")));

        m_get.push_back(SResource(new Resource(cb_all_services, 1, "services")));
        m_get.push_back(SResource(new Resource(cb_get_service, 2, "services", ":service")));
        m_get.push_back(SResource(new Resource(cb_get_all_service_listeners, 3,
                                               "services", ":service", "listeners")));
        m_get.push_back(SResource(new Resource(cb_get_service_listener, 4,
                                               "services", ":service", "listeners", "?")));

        m_get.push_back(SResource(new Resource(cb_all_filters, 1, "filters")));
        m_get.push_back(SResource(new Resource(cb_get_filter, 2, "filters", ":filter")));

        m_get.push_back(SResource(new Resource(cb_all_monitors, 1, "monitors")));
        m_get.push_back(SResource(new Resource(cb_get_monitor, 2, "monitors", ":monitor")));

        m_get.push_back(SResource(new Resource(cb_all_sessions, 1, "sessions")));
        m_get.push_back(SResource(new Resource(cb_get_session, 2, "sessions", ":session")));

        m_get.push_back(SResource(new Resource(cb_maxscale, 1, "maxscale")));
        m_get.push_back(SResource(new Resource(cb_all_threads, 2, "maxscale", "threads")));
        m_get.push_back(SResource(new Resource(cb_thread, 3, "maxscale", "threads", ":thread")));
        m_get.push_back(SResource(new Resource(cb_logs, 2, "maxscale", "logs")));
        m_get.push_back(SResource(new Resource(cb_tasks, 2, "maxscale", "tasks")));
        m_get.push_back(SResource(new Resource(cb_all_modules, 2, "maxscale", "modules")));
        m_get.push_back(SResource(new Resource(cb_module, 3, "maxscale", "modules", ":module")));

        /** For all read-only module commands */
        m_get.push_back(SResource(new Resource(cb_modulecmd, 4, "maxscale", "modules", ":module", "?")));

        m_get.push_back(SResource(new Resource(cb_all_users, 1, "users")));
        m_get.push_back(SResource(new Resource(cb_all_inet_users, 2, "users", "inet")));
        m_get.push_back(SResource(new Resource(cb_all_unix_users, 2, "users", "unix")));
        m_get.push_back(SResource(new Resource(cb_inet_user, 3, "users", "inet", ":inetuser")));
        m_get.push_back(SResource(new Resource(cb_unix_user, 3, "users", "unix", ":unixuser")));

        /** Create new resources */
        m_post.push_back(SResource(new Resource(cb_create_server, 1, "servers")));
        m_post.push_back(SResource(new Resource(cb_create_monitor, 1, "monitors")));
        m_post.push_back(SResource(new Resource(cb_create_filter, 1, "filters")));
        m_post.push_back(SResource(new Resource(cb_create_service, 1, "services")));
        m_post.push_back(SResource(new Resource(cb_create_service_listener, 3,
                                                "services", ":service", "listeners")));
        m_post.push_back(SResource(new Resource(cb_create_user, 2, "users", "inet")));
        m_post.push_back(SResource(new Resource(cb_create_user, 2, "users", "unix")));

        /** All of the above require a request body */
        for (ResourceList::iterator it = m_post.begin(); it != m_post.end(); it++)
        {
            SResource& r = *it;
            r->add_constraint(Resource::REQUIRE_BODY);
        }

        /**
         * NOTE: all POST resources added after this DO NOT require a request body.
         */

        /** For all module commands that modify state/data */
        m_post.push_back(SResource(new Resource(cb_modulecmd, 4, "maxscale", "modules", ":module", "?")));
        m_post.push_back(SResource(new Resource(cb_flush, 3, "maxscale", "logs", "flush")));

        /** Update resources */
        m_patch.push_back(SResource(new Resource(cb_alter_server, 2, "servers", ":server")));
        m_patch.push_back(SResource(new Resource(cb_alter_monitor, 2, "monitors", ":monitor")));
        m_patch.push_back(SResource(new Resource(cb_alter_service, 2, "services", ":service")));
        m_patch.push_back(SResource(new Resource(cb_alter_logs, 2, "maxscale", "logs")));
        m_patch.push_back(SResource(new Resource(cb_alter_maxscale, 1, "maxscale")));

        /** Update resource relationships directly */
        m_patch.push_back(SResource(new Resource(cb_alter_server_service_relationship, 4,
                                                 "servers", ":server", "relationships", "services")));
        m_patch.push_back(SResource(new Resource(cb_alter_server_monitor_relationship, 4,
                                                 "servers", ":server", "relationships", "monitors")));
        m_patch.push_back(SResource(new Resource(cb_alter_monitor_server_relationship, 4,
                                                 "monitors", ":monitor", "relationships", "servers")));
        m_patch.push_back(SResource(new Resource(cb_alter_service_server_relationship, 4,
                                                 "services", ":service", "relationships", "servers")));

        /** All patch resources require a request body */
        for (ResourceList::iterator it = m_patch.begin(); it != m_patch.end(); it++)
        {
            SResource& r = *it;
            r->add_constraint(Resource::REQUIRE_BODY);
        }

        /**
         * NOTE: all PATCH resources added after this DO NOT require a request body.
         */

        /** Change resource states */
        m_put.push_back(SResource(new Resource(cb_stop_monitor, 3, "monitors", ":monitor", "stop")));
        m_put.push_back(SResource(new Resource(cb_start_monitor, 3, "monitors", ":monitor", "start")));
        m_put.push_back(SResource(new Resource(cb_stop_service, 3, "services", ":service", "stop")));
        m_put.push_back(SResource(new Resource(cb_start_service, 3, "services", ":service", "start")));
        m_put.push_back(SResource(new Resource(cb_set_server, 3, "servers", ":server", "set")));
        m_put.push_back(SResource(new Resource(cb_clear_server, 3, "servers", ":server", "clear")));

        m_delete.push_back(SResource(new Resource(cb_delete_server, 2, "servers", ":server")));
        m_delete.push_back(SResource(new Resource(cb_delete_monitor, 2, "monitors", ":monitor")));
        m_delete.push_back(SResource(new Resource(cb_delete_service, 2, "services", ":service")));
        m_delete.push_back(SResource(new Resource(cb_delete_filter, 2, "filters", ":filter")));

        m_delete.push_back(SResource(new Resource(cb_delete_user, 3, "users", "inet", ":inetuser")));
        m_delete.push_back(SResource(new Resource(cb_delete_user, 3, "users", "unix", ":unixuser")));

        /** The wildcard for listener name isn't a good solution as it adds
         * a burden to the callback and requires it to do the checking but it'll
         * do for the time being */
        m_delete.push_back(SResource(new Resource(cb_delete_listener, 4,
                                                  "services", ":service", "listeners", "?")));
    }

    ~RootResource()
    {
    }

    ResourceList::const_iterator find_resource(const ResourceList& list, const HttpRequest& request) const
    {
        for (ResourceList::const_iterator it = list.begin(); it != list.end(); it++)
        {
            Resource& r = *(*it);

            if (r.match(request))
            {
                return it;
            }
        }

        return list.end();
    }

    HttpResponse process_request_type(const ResourceList& list, const HttpRequest& request)
    {
        ResourceList::const_iterator it = find_resource(list, request);

        if (it != list.end())
        {
            Resource& r = *(*it);

            if (r.requires_body() && request.get_json() == NULL)
            {
                return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error("Missing request body"));
            }

            return r.call(request);
        }

        return HttpResponse(MHD_HTTP_NOT_FOUND);
    }

    string get_supported_methods(const HttpRequest& request)
    {
        list<string> l;

        if (find_resource(m_get, request) != m_get.end())
        {
            l.push_back(MHD_HTTP_METHOD_GET);
        }
        if (find_resource(m_put, request) != m_put.end())
        {
            l.push_back(MHD_HTTP_METHOD_PUT);
        }
        if (find_resource(m_post, request) != m_post.end())
        {
            l.push_back(MHD_HTTP_METHOD_POST);
        }
        if (find_resource(m_delete, request) != m_delete.end())
        {
            l.push_back(MHD_HTTP_METHOD_DELETE);
        }

        stringstream rval;

        if (l.size() > 0)
        {
            rval << l.front();
            l.pop_front();
        }

        for (list<string>::iterator it = l.begin(); it != l.end(); it++)
        {
            rval << ", " << *it;
        }

        return rval.str();
    }

    HttpResponse process_request(const HttpRequest& request)
    {
        if (request.get_verb() == MHD_HTTP_METHOD_GET)
        {
            return process_request_type(m_get, request);
        }
        else if (request.get_verb() == MHD_HTTP_METHOD_PUT)
        {
            return process_request_type(m_put, request);
        }
        else if (request.get_verb() == MHD_HTTP_METHOD_PATCH)
        {
            return process_request_type(m_patch, request);
        }
        else if (request.get_verb() == MHD_HTTP_METHOD_POST)
        {
            return process_request_type(m_post, request);
        }
        else if (request.get_verb() == MHD_HTTP_METHOD_DELETE)
        {
            return process_request_type(m_delete, request);
        }
        else if (request.get_verb() == MHD_HTTP_METHOD_OPTIONS)
        {
            string methods = get_supported_methods(request);

            if (methods.size() > 0)
            {
                HttpResponse response(MHD_HTTP_OK);
                response.add_header(HTTP_RESPONSE_HEADER_ACCEPT, methods);
                return response;
            }
        }
        else if (request.get_verb() == MHD_HTTP_METHOD_HEAD)
        {
            /** Do a GET and just drop the body of the response */
            HttpResponse response = process_request_type(m_get, request);
            response.drop_response();
            return response;
        }

        return HttpResponse(MHD_HTTP_METHOD_NOT_ALLOWED);
    }

private:

    ResourceList m_get;    /**< GET request handlers */
    ResourceList m_put;    /**< PUT request handlers */
    ResourceList m_post;   /**< POST request handlers */
    ResourceList m_delete; /**< DELETE request handlers */
    ResourceList m_patch; /**< PATCH request handlers */
};

static RootResource resources; /**< Core resource set */
static ResourceWatcher watcher; /**< Modification watcher */
static SpinLock resource_lock;

static bool request_modifies_data(const string& verb)
{
    return verb == MHD_HTTP_METHOD_POST ||
           verb == MHD_HTTP_METHOD_PUT ||
           verb == MHD_HTTP_METHOD_DELETE ||
           verb == MHD_HTTP_METHOD_PATCH;
}

static bool request_reads_data(const string& verb)
{
    return verb == MHD_HTTP_METHOD_GET ||
           verb == MHD_HTTP_METHOD_HEAD;
}

static bool request_precondition_met(const HttpRequest& request, HttpResponse& response)
{
    bool rval = true;
    string str;
    const string& uri = request.get_uri();

    if ((str = request.get_header(MHD_HTTP_HEADER_IF_MODIFIED_SINCE)).length())
    {
        if (watcher.last_modified(uri) <= http_from_date(str))
        {
            rval = false;
            response = HttpResponse(MHD_HTTP_NOT_MODIFIED);
        }
    }
    else if ((str = request.get_header(MHD_HTTP_HEADER_IF_UNMODIFIED_SINCE)).length())
    {
        if (watcher.last_modified(uri) > http_from_date(str))
        {
            rval = false;
            response = HttpResponse(MHD_HTTP_PRECONDITION_FAILED);
        }
    }
    else if ((str = request.get_header(MHD_HTTP_HEADER_IF_MATCH)).length())
    {
        str = str.substr(1, str.length() - 2);

        if (watcher.etag(uri) != strtoul(str.c_str(), NULL, 10))
        {
            rval = false;
            response = HttpResponse(MHD_HTTP_PRECONDITION_FAILED);
        }
    }
    else if ((str = request.get_header(MHD_HTTP_HEADER_IF_NONE_MATCH)).length())
    {
        str = str.substr(1, str.length() - 2);

        if (watcher.etag(uri) == strtoul(str.c_str(), NULL, 10))
        {
            rval = false;
            response = HttpResponse(MHD_HTTP_NOT_MODIFIED);
        }
    }

    return rval;
}

static HttpResponse handle_request(const HttpRequest& request)
{
    MXS_DEBUG("%s %s %s", request.get_verb().c_str(), request.get_uri().c_str(),
              request.get_json_str().c_str());

    HttpResponse rval;

    if (request_precondition_met(request, rval))
    {
        rval = resources.process_request(request);

        if (request_modifies_data(request.get_verb()))
        {
            switch (rval.get_code())
            {
            case MHD_HTTP_OK:
            case MHD_HTTP_NO_CONTENT:
            case MHD_HTTP_CREATED:
                watcher.modify(request.get_uri());
                break;

            default:
                break;
            }
        }
        else if (request_reads_data(request.get_verb()))
        {
            const string& uri = request.get_uri();

            rval.add_header(HTTP_RESPONSE_HEADER_LAST_MODIFIED,
                            http_to_date(watcher.last_modified(uri)));

            stringstream ss;
            ss << "\"" << watcher.etag(uri) << "\"";
            rval.add_header(HTTP_RESPONSE_HEADER_ETAG, ss.str());
        }
    }

    return rval;
}

class ResourceTask: public mxs::Worker::Task
{
public:
    ResourceTask(const HttpRequest& request):
        m_request(request)
    {
    }

    void execute(mxs::Worker& worker)
    {
        m_response = handle_request(m_request);
    }

    HttpResponse result()
    {
        return m_response;
    }

private:
    const HttpRequest& m_request;
    HttpResponse m_response;
};

}

HttpResponse resource_handle_request(const HttpRequest& request)
{
    mxs::Worker* worker = mxs::RoutingWorker::get(mxs::RoutingWorker::MAIN);
    mxs::Semaphore sem;
    ResourceTask task(request);

    worker->post(&task, &sem);
    sem.wait();

    return task.result();
}
