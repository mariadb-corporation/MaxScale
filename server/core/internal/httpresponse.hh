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

#include <maxscale/ccdefs.hh>

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <microhttpd.h>

#include <maxbase/jansson.hh>
#include <maxscale/http.hh>

#include "websocket.hh"

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
    using Handler = WebSocket::Handler;
    using Callback = std::function<HttpResponse ()>;

    HttpResponse(const HttpResponse& response);
    HttpResponse& operator=(const HttpResponse& response);

    /**
     * @brief Create new HTTP response
     *
     * @param response Response body
     * @param code     HTTP return code
     */
    HttpResponse(int code = MHD_HTTP_OK, json_t* response = NULL);

    /**
     * Create a response that upgrades to a WebSocket connection
     *
     * @param handler WebSocket handler to use
     */
    HttpResponse(Handler handler);

    /**
     * Create a response from a callback
     *
     * The callback is called in a context where blocking operations are allowed. Use this if the response is
     * generated via some blocking operation.
     *
     * @param callback The callback function to be called
     */
    HttpResponse(Callback callback);

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
     * @brief Set the HTTP response code
     *
     * @param code The HTTP response code to use
     */
    void set_code(int code)
    {
        m_code = code;
    }

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
     * Adds a JWT token as a cookie
     *
     * The cookie is stored with SameSite=Strict; HttpOnly. If the REST API uses HTTPS, the cookie is stored
     * with the Secure option.
     *
     * The token should be a JWT token and it is up to the caller to make sure it is valid.
     *
     * @param name    The name of the cookie
     * @param token   The JWT token to store in the cookie
     * @param max_age Maximum age of the cookie in seconds, 0 for no maximum age
     */
    void add_cookie(const std::string& name, const std::string& token, uint32_t max_age = 0);

    /**
     * Removes a cookie
     *
     * This adds a cookie value with an expiration date in the past which is the canonical way of deleting
     * cookies.
     *
     * @param name The name of the cookie
     */
    void remove_cookie(const std::string& name);

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
     * @param json_ptr The JSON pointer that is used to extract the JSON object to be compared.
     * @param value    The string value that is used to build the filtering expression. If it's valid JSON, it
     *                 is used to match for equality.
     *
     * @return True if the filter expression was valid
     */
    bool remove_rows(const std::string& json_ptr, const std::string& value);
    // A version that uses JSON paths for the objects
    bool remove_rows_json_path(const std::string& json_path, const std::string& value);

    /**
     * Paginates the result
     *
     * Only works if the `data` member of the response JSON is an array.
     *
     * @param limit  The page size
     * @param offset The page offset, starts from 0
     */
    void paginate(int64_t limit, int64_t offset);

    Handler websocket_handler()
    {
        return m_handler;
    }

    Callback callback()
    {
        return m_cb;
    }

    /**
     * @brief Convert response to a string
     *
     * @return The request in string format
     */
    std::string to_string() const;

private:
    json_t*  m_body;    /**< Message body */
    int      m_code;    /**< The HTTP code for the response */
    Headers  m_headers; /**< Extra headers */
    Handler  m_handler; /**< WebSocket upgrade handler */
    Callback m_cb;      /**< Callback used to generate the response */

    std::vector<std::string> m_cookies;

    void remove_fields_from_resource(json_t* obj, const std::string& type,
                                     const std::unordered_set<std::string>& fields);
    void remove_fields_from_object(json_t* obj, std::vector<std::string>&& field);

    void set_cookie(const std::string& name, const std::string& token, const std::string& cookie_opts);
};
