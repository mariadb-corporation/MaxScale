#pragma once
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

/** @file REST API resources */

#include <maxscale/cppdefs.hh>

#include <string>
#include <map>
#include <tr1/memory>

#include "http.hh"
#include "httprequest.hh"
#include "httpresponse.hh"

using std::string;
using std::shared_ptr;
using std::map;

class Resource;

typedef shared_ptr<Resource>   SResource;
typedef map<string, SResource> ResourceMap;

class Resource
{
public:

    Resource() { }

    virtual ~Resource() { }

    HttpResponse process_request(HttpRequest& request)
    {
        return process_request(request, 0);
    }

protected:

    /**
     * @brief Handle a HTTP request
     *
     * This function should be overridden by the child classes.
     *
     * @param request Request to handle
     *
     * @return Response to the request
     */
    virtual HttpResponse handle(HttpRequest& request)
    {
        ss_dassert(false);
        return HttpResponse(HTTP_500_INTERNAL_SERVER_ERROR);
    };

    /**
     * Internal functions
     */
    HttpResponse process_request(HttpRequest& request, int depth);

    ResourceMap m_children; /**< Child resources */
};

/**
 * @brief Handle a HTTP request
 *
 * @param request Request to handle
 *
 * @return Response to request
 */
HttpResponse resource_handle_request(HttpRequest& request);
