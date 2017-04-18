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

#include <maxscale/spinlock.hh>

#include <maxscale/alloc.h>
#include <maxscale/jansson.hh>

#include "maxscale/resource.hh"
#include "maxscale/httprequest.hh"
#include "maxscale/httpresponse.hh"
#include "maxscale/session.h"
#include "maxscale/filter.h"
#include "maxscale/monitor.h"
#include "maxscale/service.h"

using mxs::SpinLock;
using mxs::SpinLockGuard;

HttpResponse Resource::process_request(HttpRequest& request, int depth)
{
    ResourceMap::iterator it = m_children.find(request.uri_part(depth));

    if (it != m_children.end())
    {
        return it->second->process_request(request, depth + 1);
    }

    return handle(request);
}

class ServersResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        int flags = request.get_option("pretty") == "true" ? JSON_INDENT(4) : 0;

        if (request.uri_part_count() == 1)
        {
            // TODO: Generate this via the inter-thread messaging system
            Closer<json_t*> servers(server_list_to_json());
            return HttpResponse(MHD_HTTP_OK, mxs::json_dump(servers, flags));
        }
        else
        {
            SERVER* server = server_find_by_unique_name(request.uri_part(1).c_str());

            if (server)
            {
                // TODO: Generate this via the inter-thread messaging system
                Closer<json_t*> server_js(server_to_json(server));
                // Show one server
                return HttpResponse(MHD_HTTP_OK, mxs::json_dump(server_js, flags));
            }
            else
            {
                return HttpResponse(MHD_HTTP_NOT_FOUND);
            }
        }
    }
};

class ServicesResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        int flags = request.get_option("pretty") == "true" ? JSON_INDENT(4) : 0;

        if (request.uri_part_count() == 1)
        {
            // TODO: Generate this via the inter-thread messaging system
            Closer<json_t*> all_services(service_list_to_json());
            // Show all services
            return HttpResponse(MHD_HTTP_OK, mxs::json_dump(all_services, flags));
        }
        else
        {
            SERVICE* service = service_find(request.uri_part(1).c_str());

            if (service)
            {
                Closer<json_t*> service_js(service_to_json(service));
                // Show one service
                return HttpResponse(MHD_HTTP_OK, mxs::json_dump(service_js, flags));
            }
            else
            {
                return HttpResponse(MHD_HTTP_NOT_FOUND);
            }
        }
    }
};

class FiltersResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        int flags = request.get_option("pretty") == "true" ? JSON_INDENT(4) : 0;

        if (request.uri_part_count() == 1)
        {
            Closer<json_t*> filters(filter_list_to_json());
            // Show all filters
            return HttpResponse(MHD_HTTP_OK, mxs::json_dump(filters, flags));
        }
        else
        {
            MXS_FILTER_DEF* filter = filter_def_find(request.uri_part(1).c_str());

            if (filter)
            {
                Closer<json_t*> filter_js(filter_to_json(filter));
                // Show one filter
                return HttpResponse(MHD_HTTP_OK, mxs::json_dump(filter_js, flags));
            }
            else
            {
                return HttpResponse(MHD_HTTP_NOT_FOUND);
            }
        }
    }
};

class MonitorsResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        int flags = request.get_option("pretty") == "true" ? JSON_INDENT(4) : 0;

        if (request.uri_part_count() == 1)
        {
            Closer<json_t*> monitors(monitor_list_to_json());
            // Show all monitors
            return HttpResponse(MHD_HTTP_OK, mxs::json_dump(monitors, flags));
        }
        else
        {
            MXS_MONITOR* monitor = monitor_find(request.uri_part(1).c_str());

            if (monitor)
            {
                Closer<json_t*> monitor_js(monitor_to_json(monitor));
                // Show one monitor
                return HttpResponse(MHD_HTTP_OK, mxs::json_dump(monitor_js, flags));
            }
            else
            {
                return HttpResponse(MHD_HTTP_NOT_FOUND);
            }
        }
    }
};

class SessionsResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        if (request.uri_part_count() == 1)
        {
            // Show all sessions
            return HttpResponse(MHD_HTTP_OK);
        }
        else
        {
            int id = atoi(request.uri_part(1).c_str());
            MXS_SESSION* session = session_get_by_id(id);

            if (session)
            {
                int flags = request.get_option("pretty") == "true" ? JSON_INDENT(4) : 0;
                // TODO: Generate this via the inter-thread messaging system
                Closer<json_t*> ses_json(session_to_json(session));
                session_put_ref(session);
                // Show session statistics
                return HttpResponse(MHD_HTTP_OK, mxs::json_dump(ses_json, flags));
            }
            else
            {
                return HttpResponse(MHD_HTTP_NOT_FOUND);
            }
        }
    }
};

class UsersResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        return HttpResponse(MHD_HTTP_OK);
    }
};

class LogsResource : public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        if (request.uri_part(2) == "flush")
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
        else
        {
            // Show log status
            return HttpResponse(MHD_HTTP_OK);
        }
    }
};

class ThreadsResource : public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        // Show thread status
        return HttpResponse(MHD_HTTP_OK);
    }
};

class TasksResource : public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        // Show housekeeper tasks
        return HttpResponse(MHD_HTTP_OK);
    }
};

class ModulesResource : public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        // Show modules
        return HttpResponse(MHD_HTTP_OK);
    }
};

class CoreResource: public Resource
{
public:
    CoreResource()
    {
        m_children["logs"] = SResource(new LogsResource());
        m_children["threads"] = SResource(new ThreadsResource());
        m_children["tasks"] = SResource(new TasksResource());
        m_children["modules"] = SResource(new ModulesResource());
    }

protected:
    HttpResponse handle(HttpRequest& request)
    {
        return HttpResponse(MHD_HTTP_OK);
    }
};

class RootResource: public Resource
{
public:
    RootResource()
    {
        m_children["servers"] = SResource(new ServersResource());
        m_children["services"] = SResource(new ServicesResource());
        m_children["filters"] = SResource(new FiltersResource());
        m_children["monitors"] = SResource(new MonitorsResource());
        m_children["maxscale"] = SResource(new CoreResource());
        m_children["sessions"] = SResource(new SessionsResource());
        m_children["users"] = SResource(new UsersResource());
    }

protected:
    HttpResponse handle(HttpRequest& request)
    {
        return HttpResponse(MHD_HTTP_OK);
    }
};

static RootResource resources; /**< Core resource set */
static SpinLock    resource_lock;

HttpResponse resource_handle_request(HttpRequest& request)
{
    SpinLockGuard guard(resource_lock);
    return resources.process_request(request);
}
