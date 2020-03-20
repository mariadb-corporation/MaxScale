/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/httpresponse.hh"

#include <string>
#include <sstream>
#include <sys/time.h>

#include <maxbase/alloc.h>
#include <maxscale/cn_strings.hh>

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

const HttpResponse::Headers& HttpResponse::get_headers() const
{
    return m_headers;
}

void HttpResponse::remove_fields_from_object(json_t* obj, const std::unordered_set<std::string>& fields)
{
    void* tmp;
    const char* key;
    json_t* value;

    json_object_foreach_safe(obj, tmp, key, value)
    {
        if (fields.count(key) == 0)
        {
            json_object_del(obj, key);
        }
    }
}

void HttpResponse::remove_fields_from_resource(json_t* obj, const std::string& type,
                                               const std::unordered_set<std::string>& fields)
{
    json_t* t = json_object_get(obj, CN_TYPE);

    if (json_is_string(t) && json_string_value(t) == type)
    {
        if (auto attr = json_object_get(obj, CN_ATTRIBUTES))
        {
            remove_fields_from_object(attr, fields);

            if (json_object_size(attr) == 0)
            {
                json_object_del(obj, CN_ATTRIBUTES);
            }
        }

        if (auto rel = json_object_get(obj, CN_RELATIONSHIPS))
        {
            remove_fields_from_object(rel, fields);

            if (json_object_size(rel) == 0)
            {
                json_object_del(obj, CN_RELATIONSHIPS);
            }
        }
    }
}

void HttpResponse::remove_fields(const std::string& type, const std::unordered_set<std::string>& fields)
{
    if (auto data = json_object_get(m_body, CN_DATA))
    {
        if (json_is_array(data))
        {
            json_t* val;
            size_t i;

            json_array_foreach(data, i, val)
            {
                remove_fields_from_resource(val, type, fields);
            }
        }
        else
        {
            remove_fields_from_resource(data, type, fields);
        }
    }
}
