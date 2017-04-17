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

#include "maxscale/httpresponse.hh"
#include "maxscale/admin.hh"

#include <string>
#include <sstream>

#include <maxscale/alloc.h>
#include <sys/time.h>

using std::string;
using std::stringstream;

HttpResponse::HttpResponse(enum http_code code, string response):
    m_body(response),
    m_code(code)
{
    m_headers["Date"] = http_get_date();

    // TODO: Add proper modification timestamps
    m_headers["Last-Modified"] = m_headers["Date"];
    // TODO: Add proper ETags
    m_headers["ETag"] = "bm90LXlldC1pbXBsZW1lbnRlZAo=";

    enum http_auth auth = mxs_admin_get_config().auth;

    if (auth != HTTP_AUTH_NONE)
    {
        m_headers["WWW-Authenticate"] = http_auth_to_string(auth);
    }
}

HttpResponse::~HttpResponse()
{
}

void HttpResponse::add_header(string name, string value)
{
    m_headers[name] = value;
}

string HttpResponse::get_response() const
{
    stringstream response;
    response << "HTTP/1.1 " << http_code_to_string(m_code) << "\r\n";

    for (map<string, string>::const_iterator it = m_headers.begin();
         it != m_headers.end(); it++)
    {
        response << it->first << ": " << it->second << "\r\n";
    }

    /** End of headers, add the body */
    response << "\r\n" <<  m_body;

    return response.str();
}
