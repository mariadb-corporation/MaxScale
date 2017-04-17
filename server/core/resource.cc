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
        if (request.uri_part_count() == 1)
        {
            // Show all servers
            return HttpResponse(HTTP_200_OK);
        }
        else
        {
            SERVER* server = server_find_by_unique_name(request.uri_part(1).c_str());

            if (server)
            {
                // Show one server
                return HttpResponse(HTTP_200_OK);
            }
            else
            {
                return HttpResponse(HTTP_404_NOT_FOUND);
            }
        }
    }
};

class ServicesResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        if (request.uri_part_count() == 1)
        {
            // Show all services
            return HttpResponse(HTTP_200_OK);
        }
        else
        {
            SERVICE* service = service_find(request.uri_part(1).c_str());

            if (service)
            {
                // Show one service
                return HttpResponse(HTTP_200_OK);
            }
            else
            {
                return HttpResponse(HTTP_404_NOT_FOUND);
            }
        }
    }
};

class FiltersResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        if (request.uri_part_count() == 1)
        {
            // Show all filters
            return HttpResponse(HTTP_200_OK);
        }
        else
        {
            MXS_FILTER_DEF* filter = filter_def_find(request.uri_part(1).c_str());

            if (filter)
            {
                // Show one filter
                return HttpResponse(HTTP_200_OK);
            }
            else
            {
                return HttpResponse(HTTP_404_NOT_FOUND);
            }
        }
    }
};

class MonitorsResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        if (request.uri_part_count() == 1)
        {
            // Show all monitors
            return HttpResponse(HTTP_200_OK);
        }
        else
        {
            MXS_MONITOR* monitor = monitor_find(request.uri_part(1).c_str());

            if (monitor)
            {
                // Show one monitor
                return HttpResponse(HTTP_200_OK);
            }
            else
            {
                return HttpResponse(HTTP_404_NOT_FOUND);
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
            return HttpResponse(HTTP_200_OK);
        }
        else
        {
            int id = atoi(request.uri_part(1).c_str());
            MXS_SESSION* session = session_get_by_id(id);

            if (session)
            {
                session_put_ref(session);
                // Show session statistics
                return HttpResponse(HTTP_200_OK);
            }
            else
            {
                return HttpResponse(HTTP_404_NOT_FOUND);
            }
        }
    }
};

class UsersResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        return HttpResponse(HTTP_200_OK);
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
                return HttpResponse(HTTP_200_OK);
            }
            else
            {
                return HttpResponse(HTTP_500_INTERNAL_SERVER_ERROR);
            }
        }
        else
        {
            // Show log status
            return HttpResponse(HTTP_200_OK);
        }
    }
};

class ThreadsResource : public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        // Show thread status
        return HttpResponse(HTTP_200_OK);
    }
};

class TasksResource : public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        // Show housekeeper tasks
        return HttpResponse(HTTP_200_OK);
    }
};

class ModulesResource : public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        // Show modules
        return HttpResponse(HTTP_200_OK);
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
        return HttpResponse(HTTP_200_OK);
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
        return HttpResponse(HTTP_200_OK);
    }
};

static RootResource resources; /**< Core resource set */
static SpinLock    resource_lock;

HttpResponse resource_handle_request(HttpRequest& request)
{
    SpinLockGuard guard(resource_lock);
    return resources.process_request(request);
}
