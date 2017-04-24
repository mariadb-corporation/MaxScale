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
#include "maxscale/resource.hh"

#include <list>
#include <sstream>

#include <maxscale/alloc.h>
#include <maxscale/jansson.hh>
#include <maxscale/spinlock.hh>

#include "maxscale/httprequest.hh"
#include "maxscale/httpresponse.hh"
#include "maxscale/session.h"
#include "maxscale/filter.h"
#include "maxscale/monitor.h"
#include "maxscale/service.h"
#include "maxscale/config_runtime.h"

using std::list;
using std::string;
using std::stringstream;
using mxs::SpinLock;
using mxs::SpinLockGuard;

Resource::Resource(ResourceCallback cb, int components, ...) :
    m_cb(cb)
{
    va_list args;
    va_start(args, components);

    for (int i = 0; i < components; i++)
    {
        string part = va_arg(args, const char*);
        m_path.push_back(part);
    }
    va_end(args);
}

Resource::~Resource()
{
}

bool Resource::match(const HttpRequest& request) const
{
    bool rval = false;

    if (request.uri_part_count() == m_path.size())
    {
        rval = true;

        for (size_t i = 0; i < request.uri_part_count(); i++)
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
            (path == ":monitor" && monitor_find(target.c_str())))
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
    }

    return rval;
}

HttpResponse cb_create_server(const HttpRequest& request)
{
    json_t* json = request.get_json();

    if (json)
    {
        SERVER* server = runtime_create_server_from_json(json);

        if (server)
        {
            return HttpResponse(MHD_HTTP_OK, server_to_json(server, request.host()));
        }
    }

    return HttpResponse(MHD_HTTP_BAD_REQUEST);
}

HttpResponse cb_alter_server(const HttpRequest& request)
{
    json_t* json = request.get_json();

    if (json)
    {
        SERVER* server = server_find_by_unique_name(request.uri_part(1).c_str());

        if (server && runtime_alter_server_from_json(server, json))
        {
            return HttpResponse(MHD_HTTP_OK, server_to_json(server, request.host()));
        }
    }

    return HttpResponse(MHD_HTTP_BAD_REQUEST);
}

HttpResponse cb_create_monitor(const HttpRequest& request)
{
    json_t* json = request.get_json();

    if (json)
    {
        MXS_MONITOR* monitor = runtime_create_monitor_from_json(json);

        if (monitor)
        {
            return HttpResponse(MHD_HTTP_OK, monitor_to_json(monitor, request.host()));
        }
    }

    return HttpResponse(MHD_HTTP_BAD_REQUEST);
}

HttpResponse cb_alter_monitor(const HttpRequest& request)
{
    json_t* json = request.get_json();

    if (json)
    {
        MXS_MONITOR* monitor = monitor_find(request.uri_part(1).c_str());

        if (monitor && runtime_alter_monitor_from_json(monitor, json))
        {
            return HttpResponse(MHD_HTTP_OK, monitor_to_json(monitor, request.host()));
        }
    }

    return HttpResponse(MHD_HTTP_BAD_REQUEST);
}

HttpResponse cb_all_servers(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, server_list_to_json(request.host()));
}

HttpResponse cb_get_server(const HttpRequest& request)
{
    SERVER* server = server_find_by_unique_name(request.uri_part(1).c_str());

    if (server)
    {
        return HttpResponse(MHD_HTTP_OK, server_to_json(server, request.host()));
    }

    return HttpResponse(MHD_HTTP_NOT_FOUND);
}

HttpResponse cb_all_services(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, service_list_to_json(request.host()));
}

HttpResponse cb_get_service(const HttpRequest& request)
{
    SERVICE* service = service_find(request.uri_part(1).c_str());

    if (service)
    {
        return HttpResponse(MHD_HTTP_OK, service_to_json(service, request.host()));
    }

    return HttpResponse(MHD_HTTP_NOT_FOUND);
}

HttpResponse cb_all_filters(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, filter_list_to_json(request.host()));
}

HttpResponse cb_get_filter(const HttpRequest& request)
{
    MXS_FILTER_DEF* filter = filter_def_find(request.uri_part(1).c_str());

    if (filter)
    {
        return HttpResponse(MHD_HTTP_OK, filter_to_json(filter, request.host()));
    }

    return HttpResponse(MHD_HTTP_NOT_FOUND);
}

HttpResponse cb_all_monitors(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, monitor_list_to_json(request.host()));
}

HttpResponse cb_get_monitor(const HttpRequest& request)
{
    MXS_MONITOR* monitor = monitor_find(request.uri_part(1).c_str());

    if (monitor)
    {
        return HttpResponse(MHD_HTTP_OK, monitor_to_json(monitor, request.host()));
    }

    return HttpResponse(MHD_HTTP_NOT_FOUND);
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
    // TODO: Show logs
    return HttpResponse(MHD_HTTP_OK);
}

HttpResponse cb_logs(const HttpRequest& request)
{
    // TODO: Show logs
    return HttpResponse(MHD_HTTP_OK);
}

HttpResponse cb_flush(const HttpRequest& request)
{
    // Flush logs
    if (mxs_log_rotate() == 0)
    {
        return HttpResponse(MHD_HTTP_OK);
    }
    else
    {
        return HttpResponse(MHD_HTTP_INTERNAL_SERVER_ERROR);
    }
}

HttpResponse cb_threads(const HttpRequest& request)
{
    // TODO: Show thread status
    return HttpResponse(MHD_HTTP_OK);
}

HttpResponse cb_tasks(const HttpRequest& request)
{
    // TODO: Show housekeeper tasks
    return HttpResponse(MHD_HTTP_OK);
}

HttpResponse cb_modules(const HttpRequest& request)
{
    // TODO: Show modules
    return HttpResponse(MHD_HTTP_OK);
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

    RootResource()
    {
        // Special resources required by OPTION etc.
        m_get.push_back(SResource(new Resource(cb_send_ok, 1, "/")));
        m_get.push_back(SResource(new Resource(cb_send_ok, 1, "*")));

        m_get.push_back(SResource(new Resource(cb_all_servers, 1, "servers")));
        m_get.push_back(SResource(new Resource(cb_get_server, 2, "servers", ":server")));

        m_get.push_back(SResource(new Resource(cb_all_services, 1, "services")));
        m_get.push_back(SResource(new Resource(cb_get_service, 2, "services", ":service")));

        m_get.push_back(SResource(new Resource(cb_all_filters, 1, "filters")));
        m_get.push_back(SResource(new Resource(cb_get_filter, 2, "filters", ":filter")));

        m_get.push_back(SResource(new Resource(cb_all_monitors, 1, "monitors")));
        m_get.push_back(SResource(new Resource(cb_get_monitor, 2, "monitors", ":monitor")));

        m_get.push_back(SResource(new Resource(cb_all_sessions, 1, "sessions")));
        m_get.push_back(SResource(new Resource(cb_get_session, 2, "sessions", ":session")));

        m_get.push_back(SResource(new Resource(cb_maxscale, 1, "maxscale")));
        m_get.push_back(SResource(new Resource(cb_threads, 2, "maxscale", "threads")));
        m_get.push_back(SResource(new Resource(cb_logs, 2, "maxscale", "logs")));
        m_get.push_back(SResource(new Resource(cb_tasks, 2, "maxscale", "tasks")));
        m_get.push_back(SResource(new Resource(cb_modules, 2, "maxscale", "modules")));

        m_post.push_back(SResource(new Resource(cb_flush, 3, "maxscale", "logs", "flush")));
        m_post.push_back(SResource(new Resource(cb_create_server, 1, "servers")));
        m_post.push_back(SResource(new Resource(cb_create_monitor, 1, "monitors")));

        m_put.push_back(SResource(new Resource(cb_alter_server, 2, "servers", ":server")));
        m_put.push_back(SResource(new Resource(cb_alter_monitor, 2, "monitors", ":monitor")));
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
};

static RootResource resources; /**< Core resource set */
static SpinLock resource_lock;

HttpResponse resource_handle_request(const HttpRequest& request)
{
    SpinLockGuard guard(resource_lock);
    return resources.process_request(request);
}
