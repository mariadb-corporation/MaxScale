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
        return HttpResponse(HTTP_200_OK);
    }
};

class ServicesResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        return HttpResponse(HTTP_200_OK);
    }
};

class FiltersResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        return HttpResponse(HTTP_200_OK);
    }
};

class MonitorsResource: public Resource
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
        return HttpResponse(HTTP_200_OK);
    }
};

class SessionsResource: public Resource
{
protected:
    HttpResponse handle(HttpRequest& request)
    {
        return HttpResponse(HTTP_200_OK);
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

class CoreResource: public Resource
{
public:
    CoreResource()
    {
        m_children["logs"] = SResource(new LogsResource());
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
