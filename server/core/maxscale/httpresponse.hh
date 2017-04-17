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

#include <map>
#include <string>
#include <tr1/memory>

#include <maxscale/jansson.hh>

#include "http.hh"

using std::map;
using std::string;
using std::shared_ptr;

class HttpResponse;

typedef shared_ptr<HttpResponse> SHttpResponse;

class HttpResponse
{
public:
    /**
     * @brief Create new HTTP response
     *
     * @param response Response body
     * @param code     HTTP return code
     */
    HttpResponse(string response = "", enum http_code code = HTTP_200_OK);

    ~HttpResponse();

    /**
     * @brief Add a header to the response
     *
     * @param name  Header name
     * @param value Header value
     */
    void add_header(string name, string value);

    /**
     * @brief Get the response in string format
     *
     * @return The complete response that can be sent to a client
     */
    string get_response() const;

private:
    HttpResponse(const HttpResponse&);
    HttpResponse& operator = (const HttpResponse&);

    string              m_body;    /**< Message body */
    map<string, string> m_headers; /**< Message headers */
    http_code           m_code;    /**< The HTTP code for the response */
};
