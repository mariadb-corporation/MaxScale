/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/** @file REST API resources */

#include <maxscale/ccdefs.hh>

#include <string>
#include <vector>

#include <maxscale/server.hh>
#include <maxscale/http.hh>

#include "httprequest.hh"
#include "httpresponse.hh"
#include "service.hh"
#include "filter.hh"
#include "session.hh"

typedef HttpResponse (* ResourceCallback)(const HttpRequest& request);

class Resource
{
public:
    enum resource_constraint
    {
        NONE         = 0,
        REQUIRE_BODY = (1 << 0)
    };

    template<class ... Args>
    Resource(ResourceCallback cb, Args... args)
        : m_cb(cb)
        , m_is_glob(false)
        , m_constraints(NONE)
        , m_path({args ...})
    {
        m_is_glob = std::find(m_path.begin(), m_path.end(), "?") != m_path.end();
    }

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

    ResourceCallback         m_cb;          /**< Resource handler callback */
    bool                     m_is_glob;     /**< Does this path glob? */
    uint32_t                 m_constraints; /**< Resource constraints */
    std::vector<std::string> m_path;        /**< Path components */
};

/**
 * @brief Handle a HTTP request
 *
 * @param request Request to handle
 *
 * @return Response to request
 */
HttpResponse resource_handle_request(const HttpRequest& request);
