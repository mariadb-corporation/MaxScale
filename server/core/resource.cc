/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "internal/resource.hh"

#include <vector>
#include <map>
#include <sstream>

#include <maxscale/adminusers.hh>
#include <maxbase/alloc.h>
#include <maxbase/string.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/housekeeper.h>
#include <maxscale/http.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/jansson.hh>
#include <maxscale/json_api.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/routingworker.hh>

#include "internal/config.hh"
#include "internal/config_runtime.hh"
#include "internal/filter.hh"
#include "internal/httprequest.hh"
#include "internal/httpresponse.hh"
#include "internal/modules.hh"
#include "internal/monitormanager.hh"
#include "internal/query_classifier.hh"
#include "internal/servermanager.hh"
#include "internal/service.hh"
#include "internal/session.hh"

using std::map;
using std::string;
using std::stringstream;
using maxscale::Monitor;

bool Resource::match(const HttpRequest& request) const
{
    bool rval = false;

    if (request.uri_part_count() == m_path.size() || m_is_glob)
    {
        rval = true;
        size_t parts = MXS_MIN(request.uri_part_count(), m_path.size());

        for (size_t i = 0; i < parts; i++)
        {
            if (m_path[i] != request.uri_part(i)
                && !matching_variable_path(m_path[i], request.uri_part(i)))
            {
                rval = false;
                break;
            }
        }
    }

    return rval;
}

static void remove_null_parameters(json_t* json)
{
    if (json_t* parameters = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS))
    {
        const char* key;
        json_t* value;
        void* tmp;

        json_object_foreach_safe(parameters, tmp, key, value)
        {
            if (json_is_null(value))
            {
                json_object_del(parameters, key);
            }
        }
    }
}

HttpResponse Resource::call(const HttpRequest& request) const
{
    if (json_t* json = request.get_json())
    {
        remove_null_parameters(json);
    }

    return m_cb(request);
}

bool Resource::matching_variable_path(const string& path, const string& target) const
{
    bool rval = false;

    if (path[0] == ':')
    {
        if ((path == ":service" && service_find(target.c_str()))
            || (path == ":server" && ServerManager::find_by_unique_name(target))
            || (path == ":filter" && filter_find(target.c_str()))
            || (path == ":monitor" && MonitorManager::find_monitor(target.c_str()))
            || (path == ":module" && (get_module(target.c_str(), NULL) || target == CN_CORE))
            || (path == ":inetuser" && admin_inet_user_exists(target.c_str()))
            || (path == ":unixuser" && admin_linux_account_enabled(target.c_str())))
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

bool option_rdns_is_on(const HttpRequest& request)
{
    return request.get_option("rdns") == "true";
}


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

    ResourceWatcher()
        : m_init(time(NULL))
    {
    }

    void modify(const std::string& orig_path)
    {
        std::string path = orig_path;

        do
        {
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

private:
    time_t              m_init;
    map<string, time_t> m_last_modified;
};

HttpResponse cb_stop_monitor(const HttpRequest& request)
{
    Monitor* monitor = MonitorManager::find_monitor(request.uri_part(1).c_str());
    if (monitor)
    {
        MonitorManager::stop_monitor(monitor);
    }
    return HttpResponse(MHD_HTTP_NO_CONTENT);
}

HttpResponse cb_start_monitor(const HttpRequest& request)
{
    Monitor* monitor = MonitorManager::find_monitor(request.uri_part(1).c_str());
    if (monitor)
    {
        MonitorManager::start_monitor(monitor);
    }
    return HttpResponse(MHD_HTTP_NO_CONTENT);
}

HttpResponse cb_stop_service(const HttpRequest& request)
{
    Service* service = Service::find(request.uri_part(1).c_str());
    serviceStop(service);
    return HttpResponse(MHD_HTTP_NO_CONTENT);
}

HttpResponse cb_start_service(const HttpRequest& request)
{
    Service* service = Service::find(request.uri_part(1).c_str());
    serviceStart(service);
    return HttpResponse(MHD_HTTP_NO_CONTENT);
}

HttpResponse cb_create_server(const HttpRequest& request)
{
    mxb_assert(request.get_json());

    if (runtime_create_server_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_server(const HttpRequest& request)
{
    auto server = ServerManager::find_by_unique_name(request.uri_part(1));
    mxb_assert(server && request.get_json());

    if (runtime_alter_server_from_json(server, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse do_alter_server_relationship(const HttpRequest& request, const char* type)
{
    auto server = ServerManager::find_by_unique_name(request.uri_part(1));
    mxb_assert(server && request.get_json());

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
    mxb_assert(request.get_json());

    if (runtime_create_monitor_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_create_filter(const HttpRequest& request)
{
    mxb_assert(request.get_json());

    if (runtime_create_filter_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_create_service(const HttpRequest& request)
{
    mxb_assert(request.get_json());

    if (runtime_create_service_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_create_service_listener(const HttpRequest& request)
{
    Service* service = Service::find(request.uri_part(1).c_str());
    mxb_assert(service && request.get_json());

    if (runtime_create_listener_from_json(service, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_monitor(const HttpRequest& request)
{
    Monitor* monitor = MonitorManager::find_monitor(request.uri_part(1).c_str());
    mxb_assert(monitor && request.get_json());

    if (runtime_alter_monitor_from_json(monitor, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_monitor_relationship(const HttpRequest& request, const char* type)
{
    Monitor* monitor = MonitorManager::find_monitor(request.uri_part(1).c_str());
    mxb_assert(monitor && request.get_json());

    if (runtime_alter_monitor_relationships_from_json(monitor, type, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_monitor_server_relationship(const HttpRequest& request)
{
    return cb_alter_monitor_relationship(request, CN_SERVERS);
}

HttpResponse cb_alter_monitor_service_relationship(const HttpRequest& request)
{
    return cb_alter_monitor_relationship(request, CN_SERVICES);
}

HttpResponse cb_alter_service(const HttpRequest& request)
{
    Service* service = Service::find(request.uri_part(1).c_str());
    mxb_assert(service && request.get_json());

    if (runtime_alter_service_from_json(service, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_service_relationship(const HttpRequest& request, const char* type)
{
    Service* service = Service::find(request.uri_part(1).c_str());
    mxb_assert(service && request.get_json());

    if (runtime_alter_service_relationships_from_json(service, type, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_service_server_relationship(const HttpRequest& request)
{
    return cb_alter_service_relationship(request, CN_SERVERS);
}

HttpResponse cb_alter_service_service_relationship(const HttpRequest& request)
{
    return cb_alter_service_relationship(request, CN_SERVICES);
}

HttpResponse cb_alter_service_filter_relationship(const HttpRequest& request)
{
    return cb_alter_service_relationship(request, CN_FILTERS);
}

HttpResponse cb_alter_service_monitor_relationship(const HttpRequest& request)
{
    return cb_alter_service_relationship(request, CN_MONITORS);
}

HttpResponse cb_alter_logs(const HttpRequest& request)
{
    mxb_assert(request.get_json());

    if (runtime_alter_logs_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_qc(const HttpRequest& request)
{
    mxb_assert(request.get_json());

    if (qc_alter_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_delete_server(const HttpRequest& request)
{
    auto server = ServerManager::find_by_unique_name(request.uri_part(1).c_str());
    mxb_assert(server);

    if (runtime_destroy_server(server))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_delete_monitor(const HttpRequest& request)
{
    Monitor* monitor = MonitorManager::find_monitor(request.uri_part(1).c_str());
    mxb_assert(monitor);

    if (runtime_destroy_monitor(monitor))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_delete_listener(const HttpRequest& request)
{

    Service* service = Service::find(request.uri_part(1).c_str());
    mxb_assert(service);
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
    Service* service = Service::find(request.uri_part(1).c_str());
    mxb_assert(service);

    if (runtime_destroy_service(service))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_delete_filter(const HttpRequest& request)
{
    auto filter = filter_find(request.uri_part(1).c_str());
    mxb_assert(filter);

    if (runtime_destroy_filter(filter))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}
HttpResponse cb_all_servers(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, ServerManager::server_list_to_json(request.host()));
}

HttpResponse cb_get_server(const HttpRequest& request)
{
    auto server = ServerManager::find_by_unique_name(request.uri_part(1));
    mxb_assert(server);
    return HttpResponse(MHD_HTTP_OK, ServerManager::server_to_json_resource(server, request.host()));
}

HttpResponse cb_all_services(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, service_list_to_json(request.host()));
}

HttpResponse cb_get_service(const HttpRequest& request)
{
    Service* service = Service::find(request.uri_part(1).c_str());
    mxb_assert(service);
    return HttpResponse(MHD_HTTP_OK, service_to_json(service, request.host()));
}

HttpResponse cb_get_all_service_listeners(const HttpRequest& request)
{
    Service* service = Service::find(request.uri_part(1).c_str());
    return HttpResponse(MHD_HTTP_OK, service_listener_list_to_json(service, request.host()));
}

HttpResponse cb_get_service_listener(const HttpRequest& request)
{
    Service* service = Service::find(request.uri_part(1).c_str());
    std::string listener = request.uri_part(3);
    mxb_assert(service);

    if (!service_has_named_listener(service, listener.c_str()))
    {
        return HttpResponse(MHD_HTTP_NOT_FOUND);
    }

    return HttpResponse(MHD_HTTP_OK,
                        service_listener_to_json(service,
                                                 listener.c_str(),
                                                 request.host()));
}

HttpResponse cb_all_filters(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, filter_list_to_json(request.host()));
}

HttpResponse cb_get_filter(const HttpRequest& request)
{
    auto filter = filter_find(request.uri_part(1).c_str());
    mxb_assert(filter);
    return HttpResponse(MHD_HTTP_OK, filter_to_json(filter, request.host()));
}

HttpResponse cb_all_monitors(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, MonitorManager::monitor_list_to_json(request.host()));
}

HttpResponse cb_get_monitor(const HttpRequest& request)
{
    Monitor* monitor = MonitorManager::find_monitor(request.uri_part(1).c_str());
    mxb_assert(monitor);
    return HttpResponse(MHD_HTTP_OK, MonitorManager::monitor_to_json(monitor, request.host()));
}

HttpResponse cb_all_sessions(const HttpRequest& request)
{
    bool rdns = option_rdns_is_on(request);
    return HttpResponse(MHD_HTTP_OK, session_list_to_json(request.host(), rdns));
}

HttpResponse cb_get_session(const HttpRequest& request)
{
    int id = atoi(request.uri_part(1).c_str());
    MXS_SESSION* session = session_get_by_id(id);

    if (session)
    {
        bool rdns = option_rdns_is_on(request);
        json_t* json = session_to_json(session, request.host(), rdns);
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
    mxb_assert(request.get_json());

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
    if (mxs_log_rotate())
    {
        code = MHD_HTTP_NO_CONTENT;
    }

    return HttpResponse(code);
}

HttpResponse cb_thread_rebalance(const HttpRequest& request)
{
    string thread = request.uri_part(2);
    mxb_assert(!thread.empty());    // Should have been checked already.

    long wid;
    MXB_AT_DEBUG(bool rv = ) mxb::get_long(thread, &wid);
    mxb_assert(rv);

    mxs::RoutingWorker* worker = mxs::RoutingWorker::get(wid);
    mxb_assert(worker);

    if (runtime_thread_rebalance(*worker,
                                 request.get_option("sessions"),
                                 request.get_option("recipient")))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_threads_rebalance(const HttpRequest& request)
{
    if (runtime_threads_rebalance(request.get_option("threshold")))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_all_threads(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, mxs_rworker_list_to_json(request.host()));
}

HttpResponse cb_qc(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, qc_as_json(request.host()).release());
}

HttpResponse cb_qc_classify(const HttpRequest& request)
{
    string sql = request.get_option("sql");

    return HttpResponse(MHD_HTTP_OK, qc_classify_as_json(request.host(), sql).release());
}

HttpResponse cb_qc_cache(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, qc_cache_as_json(request.host()).release());
}

HttpResponse cb_thread(const HttpRequest& request)
{
    int id = atoi(request.last_uri_part().c_str());
    return HttpResponse(MHD_HTTP_OK, mxs_rworker_to_json(request.host(), id));
}

HttpResponse cb_tasks(const HttpRequest& request)
{
    auto host = request.host();
    return HttpResponse(MHD_HTTP_OK, mxs_json_resource(host, MXS_JSON_API_TASKS, hk_tasks_json(host)));
}

HttpResponse cb_all_modules(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, module_list_to_json(request.host()));
}

HttpResponse cb_module(const HttpRequest& request)
{
    json_t* json;

    if (request.last_uri_part() == CN_CORE)
    {
        json = core_module_to_json(request.host());
    }
    else
    {
        const MXS_MODULE* module = get_module(request.last_uri_part().c_str(), NULL);
        json = module_to_json(module, request.host());
    }

    return HttpResponse(MHD_HTTP_OK, json);
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

HttpResponse cb_monitor_wait(const HttpRequest& request)
{
    MonitorManager::wait_one_tick();
    return HttpResponse(MHD_HTTP_OK);
}

HttpResponse cb_create_user(const HttpRequest& request)
{
    mxb_assert(request.get_json());

    if (runtime_create_user_from_json(request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_alter_user(const HttpRequest& request)
{
    auto user = request.last_uri_part();
    auto type = request.uri_part(1);

    if (runtime_alter_user(user, type, request.get_json()))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_delete_user(const HttpRequest& request)
{
    string user = request.last_uri_part();
    string type = request.uri_part(1);

    if ((type == CN_INET && runtime_remove_user(user.c_str(), USER_TYPE_INET))
        || (type == CN_UNIX && runtime_remove_user(user.c_str(), USER_TYPE_UNIX)))
    {
        return HttpResponse(MHD_HTTP_NO_CONTENT);
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN, runtime_get_json_error());
}

HttpResponse cb_set_server(const HttpRequest& request)
{
    SERVER* server = ServerManager::find_by_unique_name(request.uri_part(1));
    int opt = Server::status_from_string(request.get_option(CN_STATE).c_str());

    if (opt)
    {
        string errmsg;
        if (MonitorManager::set_server_status(server, opt, &errmsg))
        {
            if (status_is_in_maint(opt) && request.get_option(CN_FORCE) == CN_YES)
            {
                BackendDCB::hangup(server);
            }

            return HttpResponse(MHD_HTTP_NO_CONTENT);
        }
        else
        {
            return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error("%s", errmsg.c_str()));
        }
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN,
                        mxs_json_error("Invalid or missing value for the `%s` parameter", CN_STATE));
}

HttpResponse cb_clear_server(const HttpRequest& request)
{
    SERVER* server = ServerManager::find_by_unique_name(request.uri_part(1));
    int opt = Server::status_from_string(request.get_option(CN_STATE).c_str());

    if (opt)
    {
        string errmsg;
        if (MonitorManager::clear_server_status(server, opt, &errmsg))
        {
            return HttpResponse(MHD_HTTP_NO_CONTENT);
        }
        else
        {
            return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error("%s", errmsg.c_str()));
        }
    }

    return HttpResponse(MHD_HTTP_FORBIDDEN,
                        mxs_json_error("Invalid or missing value for the `%s` parameter", CN_STATE));
}

HttpResponse cb_modulecmd(const HttpRequest& request)
{
    std::string module = request.uri_part(2);

    // TODO: If the core ever has module commands, they need to be handled here.
    std::string identifier = request.uri_segment(3, request.uri_part_count());
    std::string verb = request.get_verb();

    const MODULECMD* cmd = modulecmd_find_command(module.c_str(), identifier.c_str());

    if (cmd)
    {
        if ((!MODULECMD_MODIFIES_DATA(cmd) && verb == MHD_HTTP_METHOD_GET)
            || (MODULECMD_MODIFIES_DATA(cmd) && verb == MHD_HTTP_METHOD_POST))
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
                modulecmd_arg_free(args);
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
                std::string self = "/";     // The uri_segment doesn't have the leading slash
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
    mxs_rworker_watchdog();
    return HttpResponse(MHD_HTTP_OK);
}

class RootResource
{
    RootResource(const RootResource&);
    RootResource& operator=(const RootResource&);
public:
    using ResourceList = std::vector<Resource>;

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
        m_get.emplace_back(cb_send_ok);
        m_get.emplace_back(cb_send_ok, "*");

        m_get.emplace_back(cb_all_servers, "servers");
        m_get.emplace_back(cb_get_server, "servers", ":server");

        m_get.emplace_back(cb_all_services, "services");
        m_get.emplace_back(cb_get_service, "services", ":service");
        m_get.emplace_back(cb_get_all_service_listeners, "services", ":service", "listeners");
        m_get.emplace_back(cb_get_service_listener, "services", ":service", "listeners", "?");

        m_get.emplace_back(cb_all_filters, "filters");
        m_get.emplace_back(cb_get_filter, "filters", ":filter");

        m_get.emplace_back(cb_all_monitors, "monitors");
        m_get.emplace_back(cb_get_monitor, "monitors", ":monitor");

        m_get.emplace_back(cb_all_sessions, "sessions");
        m_get.emplace_back(cb_get_session, "sessions", ":session");

        m_get.emplace_back(cb_maxscale, "maxscale");
        m_get.emplace_back(cb_qc, "maxscale", "query_classifier");
        m_get.emplace_back(cb_qc_classify, "maxscale", "query_classifier", "classify");
        m_get.emplace_back(cb_qc_cache, "maxscale", "query_classifier", "cache");
        m_get.emplace_back(cb_all_threads, "maxscale", "threads");
        m_get.emplace_back(cb_thread, "maxscale", "threads", ":thread");
        m_get.emplace_back(cb_logs, "maxscale", "logs");
        m_get.emplace_back(cb_tasks, "maxscale", "tasks");
        m_get.emplace_back(cb_all_modules, "maxscale", "modules");
        m_get.emplace_back(cb_module, "maxscale", "modules", ":module");

        /** For all read-only module commands */
        m_get.emplace_back(cb_modulecmd, "maxscale", "modules", ":module", "?");

        m_get.emplace_back(cb_all_users, "users");
        m_get.emplace_back(cb_all_inet_users, "users", "inet");
        m_get.emplace_back(cb_all_unix_users, "users", "unix");
        m_get.emplace_back(cb_inet_user, "users", "inet", ":inetuser");
        m_get.emplace_back(cb_unix_user, "users", "unix", ":unixuser");

        /** Debug utility endpoints */
        m_get.emplace_back(cb_monitor_wait, "maxscale", "debug", "monitor_wait");

        /** Create new resources */
        m_post.emplace_back(cb_create_server, "servers");
        m_post.emplace_back(cb_create_monitor, "monitors");
        m_post.emplace_back(cb_create_filter, "filters");
        m_post.emplace_back(cb_create_service, "services");
        m_post.emplace_back(cb_create_service_listener, "services", ":service", "listeners");
        m_post.emplace_back(cb_create_user, "users", "inet");
        m_post.emplace_back(cb_create_user, "users", "unix");

        /** All of the above require a request body */
        for (auto& r : m_post)
        {
            r.add_constraint(Resource::REQUIRE_BODY);
        }

        /**
         * NOTE: all POST resources added after this DO NOT require a request body.
         */

        /** For all module commands that modify state/data */
        m_post.emplace_back(cb_modulecmd, "maxscale", "modules", ":module", "?");
        m_post.emplace_back(cb_flush, "maxscale", "logs", "flush");
        m_post.emplace_back(cb_thread_rebalance, "maxscale", "threads", ":thread", "rebalance");
        m_post.emplace_back(cb_threads_rebalance, "maxscale", "threads", "rebalance");

        /** Update resources */
        m_patch.emplace_back(cb_alter_server, "servers", ":server");
        m_patch.emplace_back(cb_alter_monitor, "monitors", ":monitor");
        m_patch.emplace_back(cb_alter_service, "services", ":service");
        m_patch.emplace_back(cb_alter_logs, "maxscale", "logs");
        m_patch.emplace_back(cb_alter_maxscale, "maxscale");
        m_patch.emplace_back(cb_alter_qc, "maxscale", "query_classifier");
        m_patch.emplace_back(cb_alter_user, "users", "inet", ":inetuser");

        /** Update resource relationships directly */
        m_patch.emplace_back(cb_alter_server_service_relationship,
                             "servers", ":server", "relationships", "services");
        m_patch.emplace_back(cb_alter_server_monitor_relationship,
                             "servers", ":server", "relationships", "monitors");
        m_patch.emplace_back(cb_alter_monitor_server_relationship,
                             "monitors", ":monitor", "relationships", "servers");
        m_patch.emplace_back(cb_alter_monitor_service_relationship,
                             "monitors", ":monitor", "relationships", "services");
        m_patch.emplace_back(cb_alter_service_server_relationship,
                             "services", ":service", "relationships", "servers");
        m_patch.emplace_back(cb_alter_service_service_relationship,
                             "services", ":service", "relationships", "services");
        m_patch.emplace_back(cb_alter_service_filter_relationship,
                             "services", ":service", "relationships", "filters");
        m_patch.emplace_back(cb_alter_service_monitor_relationship,
                             "services", ":service", "relationships", "monitors");

        /** All patch resources require a request body */
        for (auto& r : m_patch)
        {
            r.add_constraint(Resource::REQUIRE_BODY);
        }

        /**
         * NOTE: all PATCH resources added after this DO NOT require a request body.
         */

        /** Change resource states */
        m_put.emplace_back(cb_stop_monitor, "monitors", ":monitor", "stop");
        m_put.emplace_back(cb_start_monitor, "monitors", ":monitor", "start");
        m_put.emplace_back(cb_stop_service, "services", ":service", "stop");
        m_put.emplace_back(cb_start_service, "services", ":service", "start");
        m_put.emplace_back(cb_set_server, "servers", ":server", "set");
        m_put.emplace_back(cb_clear_server, "servers", ":server", "clear");

        m_delete.emplace_back(cb_delete_server, "servers", ":server");
        m_delete.emplace_back(cb_delete_monitor, "monitors", ":monitor");
        m_delete.emplace_back(cb_delete_service, "services", ":service");
        m_delete.emplace_back(cb_delete_filter, "filters", ":filter");
        m_delete.emplace_back(cb_delete_listener, "services", ":service", "listeners", "?");

        m_delete.emplace_back(cb_delete_user, "users", "inet", ":inetuser");
        m_delete.emplace_back(cb_delete_user, "users", "unix", ":unixuser");
    }

    ~RootResource()
    {
    }

    ResourceList::const_iterator find_resource(const ResourceList& list, const HttpRequest& request) const
    {
        for (ResourceList::const_iterator it = list.begin(); it != list.end(); it++)
        {
            if (it->match(request))
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
            if (it->requires_body() && request.get_json() == NULL)
            {
                return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error("Missing request body"));
            }

            return it->call(request);
        }

        return HttpResponse(MHD_HTTP_NOT_FOUND);
    }

    string get_supported_methods(const HttpRequest& request)
    {
        std::vector<string> l;

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

        return mxb::join(l, ", ");
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

    ResourceList m_get;     /**< GET request handlers */
    ResourceList m_put;     /**< PUT request handlers */
    ResourceList m_post;    /**< POST request handlers */
    ResourceList m_delete;  /**< DELETE request handlers */
    ResourceList m_patch;   /**< PATCH request handlers */
};

static RootResource resources;      /**< Core resource set */
static ResourceWatcher watcher;     /**< Modification watcher */

static bool request_modifies_data(const string& verb)
{
    return verb == MHD_HTTP_METHOD_POST
           || verb == MHD_HTTP_METHOD_PUT
           || verb == MHD_HTTP_METHOD_DELETE
           || verb == MHD_HTTP_METHOD_PATCH;
}

static bool request_reads_data(const string& verb)
{
    return verb == MHD_HTTP_METHOD_GET
           || verb == MHD_HTTP_METHOD_HEAD;
}

static bool request_precondition_met(const HttpRequest& request, HttpResponse& response,
                                     const std::string& cksum)
{
    bool rval = false;
    const string& uri = request.get_uri();
    auto if_modified_since = request.get_header(MHD_HTTP_HEADER_IF_MODIFIED_SINCE);
    auto if_unmodified_since = request.get_header(MHD_HTTP_HEADER_IF_UNMODIFIED_SINCE);
    auto if_match = request.get_header(MHD_HTTP_HEADER_IF_MATCH);
    auto if_none_match = request.get_header(MHD_HTTP_HEADER_IF_NONE_MATCH);

    if ((!if_unmodified_since.empty() && watcher.last_modified(uri) > http_from_date(if_unmodified_since))
        || (!if_match.empty() && cksum != if_match))
    {
        response = HttpResponse(MHD_HTTP_PRECONDITION_FAILED);
    }
    else if (!if_modified_since.empty() || !if_none_match.empty())
    {
        if ((if_modified_since.empty() || watcher.last_modified(uri) <= http_from_date(if_modified_since))
            && (if_none_match.empty() || cksum == if_none_match))
        {
            response = HttpResponse(MHD_HTTP_NOT_MODIFIED);
        }
    }
    else
    {
        rval = true;
    }

    return rval;
}

static void remove_unwanted_fields(const HttpRequest& request, HttpResponse& response)
{
    for (const auto& a : request.get_options())
    {
        const char FIELDS[] = "fields[";
        auto s = a.first.substr(0, sizeof(FIELDS) - 1);

        if (s == FIELDS && a.first.back() == ']')
        {
            auto type = a.first.substr(s.size(), a.first.size() - s.size() - 1);
            auto fields = mxb::strtok(a.second, ",");

            if (!fields.empty())
            {
                response.remove_fields(type, {fields.begin(), fields.end()});
            }
        }
    }
}

static HttpResponse handle_request(const HttpRequest& request)
{
    MXS_DEBUG("%s %s %s",
              request.get_verb().c_str(),
              request.get_uri().c_str(),
              request.get_json_str().c_str());

    HttpResponse rval = resources.process_request(request);

    // Calculate the checksum from the generated JSON
    auto str = mxs::json_dump(rval.get_response());
    auto cksum = '"' + mxs::checksum<mxs::SHA1Checksum>((uint8_t*)str.c_str(), str.size()) + '"';

    if (request_precondition_met(request, rval, cksum))
    {
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
            const auto& uri = request.get_uri();
            rval.add_header(HTTP_RESPONSE_HEADER_LAST_MODIFIED, http_to_date(watcher.last_modified(uri)));
            rval.add_header(HTTP_RESPONSE_HEADER_ETAG, cksum.c_str());
        }

        remove_unwanted_fields(request, rval);
    }

    return rval;
}
}

HttpResponse resource_handle_request(const HttpRequest& request)
{
    mxb::WatchedWorker* worker = mxs::MainWorker::get();
    HttpResponse response;

    if (!worker->call([&request, &response, worker]() {
                          mxb::WatchdogNotifier::Workaround workaround(worker);
                          response = handle_request(request);
                      }, mxb::Worker::EXECUTE_AUTO))
    {
        response = HttpResponse(MHD_HTTP_SERVICE_UNAVAILABLE);
    }

    return response;
}
