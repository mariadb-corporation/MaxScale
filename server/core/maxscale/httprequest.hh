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

#include <maxscale/cppdefs.hh>

#include <deque>
#include <map>
#include <string>
#include <tr1/memory>
#include <cstdint>
#include <microhttpd.h>

#include <maxscale/jansson.hh>
#include <maxscale/utils.hh>

#include "http.hh"

// The API version part of the URL
#define MXS_REST_API_VERSION "v1"

static int value_iterator(void *cls,
                          enum MHD_ValueKind kind,
                          const char *key,
                          const char *value)
{
    std::pair<std::string, std::string>* cmp = (std::pair<std::string, std::string>*)cls;

    if (strcasecmp(cmp->first.c_str(), key) == 0)
    {
        cmp->second = value;
        return MHD_NO;
    }

    return MHD_YES;
}

class HttpRequest
{
    HttpRequest(const HttpRequest&);
    HttpRequest& operator = (const HttpRequest);
public:
    /**
     * @brief Parse a request
     *
     * @param request Request to parse
     *
     * @return Parsed statement or NULL if request is not valid
     */
    HttpRequest(struct MHD_Connection *connection, std::string url, std::string method, json_t* data);

    ~HttpRequest();

    /**
     * @brief Return request verb type
     *
     * @return One of the HTTP verb values
     */
    const std::string& get_verb() const
    {
        return m_verb;
    }

    /**
     * @brief Get header value
     *
     * @param header Header to get
     *
     * @return Header value or empty string if the header was not found
     */
    std::string get_header(const std::string& header) const
    {
        std::pair<std::string, std::string> p;
        p.first = header;

        MHD_get_connection_values(m_connection, MHD_HEADER_KIND,
                                  value_iterator, &p);

        return p.second;
    }

    /**
     * @brief Get option value
     *
     * @param header Option to get
     *
     * @return Option value or empty string if the option was not found
     */
    std::string get_option(const std::string& option) const
    {
        std::pair<std::string, std::string> p;
        p.first = option;

        MHD_get_connection_values(m_connection, MHD_GET_ARGUMENT_KIND,
                                  value_iterator, &p);

        return p.second;
    }

    /**
     * @brief Return request body
     *
     * @return Request body or empty string if no body is defined
     */
    const std::string& get_json_str() const
    {
        return m_json_string;
    }

    /**
     * @brief Return raw JSON body
     *
     * @return Raw JSON body or NULL if no body is defined
     */
    json_t* get_json() const
    {
        return m_json.get();
    }

    /**
     * @brief Get complete request URI
     *
     * @return The complete request URI
     */
    const std::string& get_uri() const
    {
        return m_resource;
    }

    /**
     * @brief Get URI part
     *
     * @param idx Zero indexed part number in URI
     *
     * @return The request URI part or empty string if no part was found
     */
    const std::string uri_part(uint32_t idx) const
    {
        return m_resource_parts.size() > idx ? m_resource_parts[idx] : "";
    }

    /**
     * @brief Return how many parts are in the URI
     *
     * @return Number of URI parts
     */
    size_t uri_part_count() const
    {
        return m_resource_parts.size();
    }

    const char* host() const
    {
        return m_hostname.c_str();
    }

    /**
     * @brief Drop the API version prefix
     *
     * @return True if prefix is present and was successfully removed
     */
    bool validate_api_version();

private:

    /** Constants */
    static const std::string HTTP_PREFIX;
    static const std::string HTTPS_PREFIX;

    std::map<std::string, std::string> m_options;        /**< Request options */
    mxs::Closer<json_t*>               m_json;           /**< Request body */
    std::string                        m_json_string;    /**< String version of @c m_json */
    std::string                        m_resource;       /**< Requested resource */
    std::deque<std::string>            m_resource_parts; /**< @c m_resource split into parts */
    std::string                        m_verb;           /**< Request method */
    std::string                        m_hostname;       /**< The value of the Host header */
    struct MHD_Connection*             m_connection;
};
