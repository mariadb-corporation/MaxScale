/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include "internal/httpresponse.hh"

#include <string>
#include <sstream>

#include <maxscale/alloc.h>
#include <sys/time.h>

#include "internal/admin.hh"

using std::string;
using std::stringstream;

HttpResponse::HttpResponse(int code, json_t* response)
    : m_body(response)
    , m_code(code)
{
    string http_date = http_get_date();
    add_header(HTTP_RESPONSE_HEADER_DATE, http_date);

    if (m_body)
    {
        add_header(HTTP_RESPONSE_HEADER_CONTENT_TYPE, "application/json");
    }
}

HttpResponse::HttpResponse(const HttpResponse& response)
    : m_body(json_incref(response.m_body))
    , m_code(response.m_code)
    , m_headers(response.m_headers)
{
}

HttpResponse& HttpResponse::operator=(const HttpResponse& response)
{
    json_t* body = m_body;
    m_body = json_incref(response.m_body);
    m_code = response.m_code;
    m_headers = response.m_headers;
    json_decref(body);
    return *this;
}

HttpResponse::~HttpResponse()
{
    if (m_body)
    {
        json_decref(m_body);
    }
}

json_t* HttpResponse::get_response() const
{
    return m_body;
}

void HttpResponse::drop_response()
{
    json_decref(m_body);
    m_body = NULL;
}

int HttpResponse::get_code() const
{
    return m_code;
}

void HttpResponse::add_header(const string& key, const string& value)
{
    m_headers[key] = value;
}

const Headers& HttpResponse::get_headers() const
{
    return m_headers;
}
