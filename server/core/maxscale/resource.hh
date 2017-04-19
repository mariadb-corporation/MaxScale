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
#include <deque>
#include <tr1/memory>

#include <maxscale/server.h>

#include "http.hh"
#include "httprequest.hh"
#include "httpresponse.hh"
#include "monitor.h"
#include "service.h"
#include "filter.h"
#include "session.h"

using std::string;
using std::shared_ptr;
using std::deque;

typedef HttpResponse (*ResourceCallback)(HttpRequest& request);

class Resource
{
public:

    Resource(ResourceCallback cb, int components, ...);
    ~Resource();

    /**
     * @brief Check if a request matches this resource
     *
     * @param request Request to match
     *
     * @return True if this request matches this resource
     */
    bool match(HttpRequest& request);

    /**
     * @brief Handle a HTTP request
     *
     * @param request Request to handle
     *
     * @return Response to the request
     */
    HttpResponse call(HttpRequest& request);

private:

    bool matching_variable_path(const string& path, const string& target);

    ResourceCallback m_cb; /**< Resource handler callback */
    deque<string>    m_path; /**< Path components */
};

/**
 * @brief Handle a HTTP request
 *
 * @param request Request to handle
 *
 * @return Response to request
 */
HttpResponse resource_handle_request(HttpRequest& request);
