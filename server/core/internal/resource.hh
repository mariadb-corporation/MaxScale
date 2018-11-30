/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

/** @file REST API resources */

#include <maxscale/ccdefs.hh>

#include <string>
#include <deque>

#include <maxscale/server.h>
#include <maxscale/http.hh>

#include "httprequest.hh"
#include "httpresponse.hh"
#include "monitor.hh"
#include "service.hh"
#include "filter.hh"
#include "session.hh"

typedef HttpResponse (* ResourceCallback)(const HttpRequest& request);

class Resource
{
    Resource(const Resource&);
    Resource& operator=(const Resource&);
public:

    enum resource_constraint
    {
        NONE         = 0,
        REQUIRE_BODY = (1 << 0)
    };

    Resource(ResourceCallback cb, int components, ...);
    ~Resource();

    /**
     * @brief Check if a request matches this resource
     *
     * @param request Request to match
     *
     * @return True if this request matches this resource
     */
    bool match(const HttpRequest& request) const;

    /**
     * @brief Handle a HTTP request
     *
     * @param request Request to handle
     *
     * @return Response to the request
     */
    HttpResponse call(const HttpRequest& request) const;

    /**
     * Add a resource constraint
     *
     * @param type Constraint to add
     */
    void add_constraint(resource_constraint type);

    /**
     * Whether resource requires a request body
     *
     * @return True if resource requires a request body
     */
    bool requires_body() const;

private:

    bool matching_variable_path(const std::string& path, const std::string& target) const;

    ResourceCallback        m_cb;           /**< Resource handler callback */
    std::deque<std::string> m_path;         /**< Path components */
    bool                    m_is_glob;      /**< Does this path glob? */
    uint32_t                m_constraints;  /**< Resource constraints */
};

/**
 * @brief Handle a HTTP request
 *
 * @param request Request to handle
 *
 * @return Response to request
 */
HttpResponse resource_handle_request(const HttpRequest& request);
