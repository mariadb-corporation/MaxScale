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
#include <microhttpd.h>

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
    HttpResponse(int code = MHD_HTTP_OK, json_t* response = NULL);
    HttpResponse(const HttpResponse& response);
    HttpResponse& operator = (const HttpResponse& response);

    ~HttpResponse();

    /**
     * @brief Get the response body
     *
     * @return The response body
     */
    json_t* get_response() const;

    /**
     * @brief Get the HTTP response code
     *
     * @return The HTTP response code
     */
    int get_code() const;

private:
    json_t* m_body;    /**< Message body */
    int     m_code;    /**< The HTTP code for the response */
};
