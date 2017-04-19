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
using mxs::SpinLock;
using mxs::SpinLockGuard;

Resource::Resource(ResourceCallback cb, int components, ...):
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

Resource::~Resource() { }

bool Resource::match(HttpRequest& request)
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

HttpResponse Resource::call(HttpRequest& request)
{
    return m_cb(request);
};

bool Resource::matching_variable_path(const string& path, const string& target)
{
    bool rval = false;

    if (path[0] == ':')
    {
        if ((path == ":service" && service_find(target.c_str())) ||
            (path == ":server"  && server_find_by_unique_name(target.c_str())) ||
            (path == ":filter"  && filter_def_find(target.c_str())) ||
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

HttpResponse cb_create_server(HttpRequest& request)
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

    return HttpResponse(MHD_HTTP_INTERNAL_SERVER_ERROR);
}

HttpResponse cb_all_servers(HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, server_list_to_json(request.host()));
}

HttpResponse cb_get_server(HttpRequest& request)
{
    SERVER* server = server_find_by_unique_name(request.uri_part(1).c_str());

    if (server)
    {
        return HttpResponse(MHD_HTTP_OK, server_to_json(server, request.host()));
    }

    return HttpResponse(MHD_HTTP_NOT_FOUND);
}

HttpResponse cb_all_services(HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, service_list_to_json(request.host()));
}

HttpResponse cb_get_service(HttpRequest& request)
{
    SERVICE* service = service_find(request.uri_part(1).c_str());

    if (service)
    {
        return HttpResponse(MHD_HTTP_OK, service_to_json(service, request.host()));
    }

    return HttpResponse(MHD_HTTP_NOT_FOUND);
}

HttpResponse cb_all_filters(HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, filter_list_to_json(request.host()));
}

HttpResponse cb_get_filter(HttpRequest& request)
{
    MXS_FILTER_DEF* filter = filter_def_find(request.uri_part(1).c_str());

    if (filter)
    {
        return HttpResponse(MHD_HTTP_OK, filter_to_json(filter, request.host()));
    }

    return HttpResponse(MHD_HTTP_NOT_FOUND);
}

HttpResponse cb_all_monitors(HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, monitor_list_to_json(request.host()));
}

HttpResponse cb_get_monitor(HttpRequest& request)
{
    MXS_MONITOR* monitor = monitor_find(request.uri_part(1).c_str());

    if (monitor)
    {
        return HttpResponse(MHD_HTTP_OK, monitor_to_json(monitor, request.host()));
    }

    return HttpResponse(MHD_HTTP_NOT_FOUND);
}

HttpResponse cb_all_sessions(HttpRequest& request)
{
    // TODO: Implement this
    return HttpResponse(MHD_HTTP_OK);
}

HttpResponse cb_get_session(HttpRequest& request)
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

HttpResponse cb_maxscale(HttpRequest& request)
{
    // TODO: Show logs
    return HttpResponse(MHD_HTTP_OK);
}

HttpResponse cb_logs(HttpRequest& request)
{
    // TODO: Show logs
    return HttpResponse(MHD_HTTP_OK);
}

HttpResponse cb_flush(HttpRequest& request)
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

HttpResponse cb_threads(HttpRequest& request)
{
    // TODO: Show thread status
    return HttpResponse(MHD_HTTP_OK);
}

HttpResponse cb_tasks(HttpRequest& request)
{
    // TODO: Show housekeeper tasks
    return HttpResponse(MHD_HTTP_OK);
}

HttpResponse cb_modules(HttpRequest& request)
{
    // TODO: Show modules
    return HttpResponse(MHD_HTTP_OK);
}

class RootResource
{
public:
    typedef std::shared_ptr<Resource> SResource;
    typedef list<SResource> ResourceList;

    RootResource()
    {
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
    }

    ~RootResource()
    {
    }

    HttpResponse process_request_type(ResourceList& list, HttpRequest& request)
    {
        for (ResourceList::iterator it = list.begin(); it != list.end(); it++)
        {
            Resource& r = *(*it);

            if (r.match(request))
            {
                return r.call(request);
            }
        }

        return HttpResponse(MHD_HTTP_NOT_FOUND);
    }

    HttpResponse process_request(HttpRequest& request)
    {
        if (request.get_verb() == "GET")
        {
            return process_request_type(m_get, request);
        }
        else if (request.get_verb() == "PUT")
        {
            return process_request_type(m_put, request);
        }
        else if (request.get_verb() == "POST")
        {
            return process_request_type(m_post, request);
        }

        return HttpResponse(MHD_HTTP_METHOD_NOT_ALLOWED);
    }

private:

    ResourceList m_get;  /**< GET request handlers */
    ResourceList m_put;  /**< PUT request handlers */
    ResourceList m_post; /**< POST request handlers */
};

static RootResource resources; /**< Core resource set */
static SpinLock    resource_lock;

HttpResponse resource_handle_request(HttpRequest& request)
{
    SpinLockGuard guard(resource_lock);
    return resources.process_request(request);
}
