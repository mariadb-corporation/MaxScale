/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
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
        REQUIRE_BODY = (1 << 0),
        REQUIRE_SYNC = (1 << 1),
    };

    template<class ... Args>
    Resource(uint32_t constraints, ResourceCallback cb, Args... args)
        : m_cb(cb)
        , m_is_glob(false)
        , m_constraints(constraints)
        , m_path({args ...})
    {
        m_is_glob = std::find(m_path.begin(), m_path.end(), "?") != m_path.end();
    }

    template<class ... Args>
    Resource(ResourceCallback cb, Args... args)
        : Resource(NONE, cb, args...)
    {
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
     * @brief Check if the given part of the path matches the given value
     *
     * @param part  Part to match against
     * @param depth The index number of the path part to compare to
     *
     * @return True if the part matches
     */
    bool part_matches(const std::string& part, size_t depth) const;

    /**
     * @brief Check if all parts except the variable ones match
     *
     * @param path The full path to match against
     *
     * @return True if only the variable part does not match
     */
    bool variable_part_mismatch(const std::deque<std::string>& path) const;

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

    /**
     * Whether resource must be synchronized to the cluster
     *
     * @return True if resource requires synchronization
     */
    bool requires_sync() const;

    /**
     * The components of the path
     *
     * @return The components of the broken down path.
     */
    const std::vector<std::string>& path() const
    {
        return m_path;
    }

    /**
     * Comparison operator, used to sort the resources
     */
    bool operator<(const Resource& other) const
    {
        return m_path < other.m_path;
    }

private:

    bool is_variable_part(size_t i) const;
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

/**
 * Get MaxScale logs as JSON
 *
 * @param host   The hostname of this MaxScale, sent by the client.
 *
 * @return The logs as a JSON API resource.
 */
json_t* mxs_logs_to_json(const char* host);

/**
 * Get MaxScale log data as JSON
 *
 * @param host     The hostname of this MaxScale, sent by the client.
 * @param cursor   The cursor where to read log entries for. An empty string means no cursor is open.
 * @param rows     How many rows of logs to read.
 * @param priority Log priorities to include or empty set for all priorities
 *
 * @return The log data as a JSON API resource.
 */
json_t* mxs_log_data_to_json(const char* host, const std::string& cursor, int rows,
                             const std::set<std::string>& priorities);

// Same as mxs_log_data_to_json except that this is a resouce collection which allows rows to be filtered
// using the `filter` request option.
json_t* mxs_log_entries_to_json(const char* host, const std::string& cursor, int rows,
                                const std::set<std::string>& priorities);

/**
 * Create a stream of logs
 *
 * @param cursor   The cursor where to stream the entries for. An empty cursor means start
 *                 from the latest position.
 * @param priority Log priorities to include or empty set for all priorities
 *
 * @return Function that can be called to read the log. If an empty string is returned, the current
 *         end of the log is reached. Calling it again can return more data at a later time.
 */
std::function<std::string()> mxs_logs_stream(const std::string& cursor,
                                             const std::set<std::string>& priorities);
