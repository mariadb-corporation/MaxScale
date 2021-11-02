/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <microhttpd.h>

#include <maxscale/jansson.hh>
#include <maxscale/http.hh>

/**
 * A list of default headers that are generated with each response
 */
#define HTTP_RESPONSE_HEADER_DATE          "Date"
#define HTTP_RESPONSE_HEADER_LAST_MODIFIED "Last-Modified"
#define HTTP_RESPONSE_HEADER_ETAG          "ETag"
#define HTTP_RESPONSE_HEADER_ACCEPT        "Accept"
#define HTTP_RESPONSE_HEADER_CONTENT_TYPE  "Content-Type"

class HttpResponse
{
public:
    using Headers = std::unordered_map<std::string, std::string>;

    /**
     * @brief Create new HTTP response
     *
     * @param response Response body
     * @param code     HTTP return code
     */
    HttpResponse(int code = MHD_HTTP_OK, json_t* response = NULL);
    HttpResponse(const HttpResponse& response);
    HttpResponse& operator=(const HttpResponse& response);

    ~HttpResponse();

    /**
     * @brief Get the response body
     *
     * @return The response body
     */
    json_t* get_response() const;

    /**
     * @brief Drop response body
     *
     * This discards the message body.
     */
    void drop_response();

    /**
     * @brief Get the HTTP response code
     *
     * @return The HTTP response code
     */
    int get_code() const;

    /**
     * @brief Add an extra header to this response
     *
     * @param key   Header name
     * @param value Header value
     */
    void add_header(const std::string& key, const std::string& value);

    /**
     * @brief Get request headers
     *
     * @return Headers of this request
     */
    const Headers& get_headers() const;

    /**
     * Returns cookies added to the response
     *
     * @return The cookies that have been set
     */
    const std::vector<std::string>& cookies() const
    {
        return m_cookies;
    }

    /**
     * Add a cookie to the response
     *
     * @param cookie Cookie to add, the value in `Set-Cookie: <value>`
     */
    void add_cookie(const std::string& cookie)
    {
        m_cookies.push_back(cookie);
    }

    /**
     * Removes fields from the response
     *
     * @param type   The JSON API object type (e.g. services for a single service or a collection of services)
     * @param fields The fields to include
     */
    void remove_fields(const std::string& type, const std::unordered_set<std::string>& fields);

    /**
     * Removes rows from the response
     *
     * Any values to do not compare equal to the given JSON value are discarded.
     *
     * @param json_ptr The JSON pointer to use
     * @param value    The value to compare to
     */
    void remove_rows(const std::string& json_ptr, json_t* value);

private:
    json_t* m_body;     /**< Message body */
    int     m_code;     /**< The HTTP code for the response */
    Headers m_headers;  /**< Extra headers */

    std::vector<std::string> m_cookies;

    void remove_fields_from_resource(json_t* obj, const std::string& type,
                                     const std::unordered_set<std::string>& fields);
    void remove_fields_from_object(json_t* obj, std::vector<std::string>&& field);
};
