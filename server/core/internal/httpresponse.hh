/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
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
    using Callback = std::function<HttpResponse()>;

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
     * Adds a JWT token as a cookie split into a public and private part
     *
     * The cookie is stored using SameSite=Lax for the public part and with SameSite=Strict; HttpOnly for the
     * private part. If the REST API uses HTTPS, then both cookies are stored with the Secure option.
     *
     * The token must be a JWT token and it is up to the caller to make sure it is valid.
     *
     * @param public_name   The name of the cookie where the public part is stored
     * @param private_name  The name of the cookie where the private part is stored
     * @param token         The JWT token to store in the cookie
     * @param max_age       Maximum age of the cookie in seconds, 0 for no maximum age
     */
    void add_split_cookie(const std::string& public_name, const std::string& private_name,
                          const std::string& token, uint32_t max_age = 0);

    /**
     * Removes a cookie split into a public and private part
     *
     * This adds cookie values with an expiration date in the past which is the canonical way of deleting
     * cookies.
     *
     * @param public_name   The name of the cookie where the public part is stored
     * @param private_name  The name of the cookie where the private part is stored
     */
    void remove_split_cookie(const std::string& public_name, const std::string& private_name);

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

    Handler websocket_handler()
    {
        return m_handler;
    }

    Callback callback()
    {
        return m_cb;
    }

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
};
