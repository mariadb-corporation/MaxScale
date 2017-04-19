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

#include <string>
#include <sstream>

#include <maxscale/alloc.h>
#include <sys/time.h>

#include "maxscale/admin.hh"

using std::string;
using std::stringstream;

HttpResponse::HttpResponse(int code, json_t* response):
    m_body(response),
    m_code(code)
{
}

HttpResponse::HttpResponse(const HttpResponse& response):
    m_body(response.m_body),
    m_code(response.m_code)
{
    json_incref(m_body);
}

HttpResponse& HttpResponse::operator=(const HttpResponse& response)
{
    json_t* body = m_body;
    m_body = json_incref(response.m_body);
    m_code = response.m_code;
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

int HttpResponse::get_code() const
{
    return m_code;
}
