/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/httpresponse.hh"

#include <string>
#include <sstream>
#include <sys/time.h>

#include <maxscale/cn_strings.hh>
#include <maxscale/json_api.hh>

#include "internal/admin.hh"

using std::string;
using std::stringstream;

namespace
{
bool json_ptr_matches(const std::string& json_ptr, json_t* obj, json_t* rhs)
{
    auto lhs = mxb::json_ptr(obj, json_ptr.c_str());
    return lhs && json_equal(lhs, rhs);
}
}

HttpResponse::HttpResponse(int code, json_t* response)
    : m_body(response)
    , m_code(code)
    , m_headers{{HTTP_RESPONSE_HEADER_DATE, http_get_date()}}
{
    if (m_body)
    {
        add_header(HTTP_RESPONSE_HEADER_CONTENT_TYPE, "application/json");
    }
}

HttpResponse::HttpResponse(Handler handler)
    : HttpResponse(MHD_HTTP_SWITCHING_PROTOCOLS)
{
    m_handler = handler;
}

HttpResponse::HttpResponse(Callback callback)
    : HttpResponse(MHD_HTTP_BAD_REQUEST)
{
    m_cb = callback;
}

HttpResponse::HttpResponse(const HttpResponse& response)
    : m_body(json_incref(response.m_body))
    , m_code(response.m_code)
    , m_headers(response.m_headers)
    , m_handler(response.m_handler)
    , m_cb(response.m_cb)
    , m_cookies(response.m_cookies)
{
}

HttpResponse& HttpResponse::operator=(const HttpResponse& response)
{
    json_t* body = m_body;
    m_body = json_incref(response.m_body);
    m_code = response.m_code;
    m_headers = response.m_headers;
    m_handler = response.m_handler;
    m_cb = response.m_cb;
    m_cookies = response.m_cookies;
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

void HttpResponse::remove_fields_from_object(json_t* obj, std::vector<std::string>&& fields)
{
    if (fields.empty())
    {
        return;
    }

    if (json_is_object(obj))
    {
        if (json_t* p = json_object_get(obj, fields.front().c_str()))
        {
            // Remove all other keys
            json_incref(p);
            json_object_clear(obj);
            json_object_set_new(obj, fields.front().c_str(), p);

            fields.erase(fields.begin());
            remove_fields_from_object(p, std::move(fields));
        }
        else
        {
            json_object_clear(obj);
        }
    }
    else
    {
        json_object_clear(obj);
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
            auto newattr = json_object();

            for (const auto& a : fields)
            {
                auto tmp = json_deep_copy(attr);
                remove_fields_from_object(tmp, mxb::strtok(a, "/"));
                json_object_update_recursive(newattr, tmp);
                json_decref(tmp);
            }

            json_object_set_new(obj, CN_ATTRIBUTES, newattr);

            if (json_object_size(newattr) == 0)
            {
                json_object_del(obj, CN_ATTRIBUTES);
            }
        }

        if (auto rel = json_object_get(obj, CN_RELATIONSHIPS))
        {
            auto newrel = json_object();

            for (const auto& a : fields)
            {
                auto tmp = json_deep_copy(rel);
                remove_fields_from_object(tmp, mxb::strtok(a, "/"));
                json_object_update_recursive(newrel, tmp);
                json_decref(tmp);
            }

            json_object_set_new(obj, CN_RELATIONSHIPS, newrel);

            if (json_object_size(newrel) == 0)
            {
                json_object_del(obj, CN_RELATIONSHIPS);
            }
        }
    }
}

void HttpResponse::add_cookie(const std::string& name, const std::string& token, uint32_t max_age)
{
    std::string cookie_opts = "; Path=/";

    if (max_age)
    {
        cookie_opts += "; Max-Age=" + std::to_string(max_age);
    }

    set_cookie(name, token, cookie_opts);
}

void HttpResponse::remove_cookie(const std::string& name)
{
    set_cookie(name, "", "; Path=/; Expires=" + http_to_date(0));
}

void HttpResponse::set_cookie(const std::string& name,
                              const std::string& token,
                              const std::string& cookie_opts)
{
    const bool cors = mxs_admin_use_cors();
    std::string secure_opt = mxs_admin_https_enabled() || cors ? "; Secure" : "";
    std::string priv_opts = "; SameSite=Strict; HttpOnly";

    if (cors)
    {
        priv_opts = "; SameSite=None; HttpOnly";
    }

    add_cookie(name + "=" + token + cookie_opts + secure_opt + priv_opts);
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

void HttpResponse::remove_rows(const std::string& json_ptr, json_t* json)
{
    if (auto data = json_object_get(m_body, CN_DATA))
    {
        if (json_is_array(data))
        {
            json_t* val;
            size_t i;
            json_t* new_arr = json_array();

            json_array_foreach(data, i, val)
            {
                if (json_ptr_matches(json_ptr, val, json))
                {
                    json_array_append_new(new_arr, json_copy(val));
                }
            }

            json_object_set_new(m_body, CN_DATA, new_arr);
        }
    }
}

void HttpResponse::paginate(int64_t limit, int64_t offset)
{
    mxb_assert(limit > 0);

    if (auto data = json_object_get(m_body, CN_DATA))
    {
        if (json_is_array(data))
        {
            int64_t total_size = json_array_size(data);

            // Don't actually paginate the data if only one page would be generated
            if (total_size > limit)
            {
                json_t* new_data = json_array();

                for (int64_t i = offset * limit; i < (offset + 1) * limit; i++)
                {
                    if (json_t* v = json_array_get(data, i))
                    {
                        json_array_append(new_data, v);
                    }
                }

                json_object_set_new(m_body, CN_DATA, new_data);
            }

            // Create pagination links only if the resource itself didn't create them. The /maxscale/logs/data
            // endpoint has its own pagination links and they must not be overwritten.
            json_t* links = json_object_get(m_body, CN_LINKS);

            if (links && !json_object_get(links, "next") && !json_object_get(links, "prev")
                && !json_object_get(links, "last") && !json_object_get(links, "first"))
            {
                mxb_assert(json_object_get(links, "self"));
                const std::string LB = "%5B";   // Percent-encoded [
                const std::string RB = "%5D";   // Percent-encoded ]

                std::string base = json_string_value(json_object_get(links, "self"));
                base += "?page" + LB + "size" + RB + "=" + std::to_string(limit)
                    + "&page" + LB + "number" + RB + "=";

                // Generate the paginated self link
                auto self = base + std::to_string(offset);
                json_object_set_new(links, "self", json_string(self.c_str()));

                if ((offset + 1) * limit < total_size)
                {
                    // More pages available
                    auto next = base + std::to_string(offset + 1);
                    json_object_set_new(links, "next", json_string(next.c_str()));
                }

                auto first = base + std::to_string(0);
                json_object_set_new(links, "first", json_string(first.c_str()));

                // The same calculation that is used to count how many bytes are needed for N bits can be used
                // to determine how many pages we have: (bits + 7) / 8 = bytes
                // Pages are indexed from 0 so we decrement the value by one.
                auto last = base + std::to_string(((total_size + (limit - 1)) / limit) - 1);
                json_object_set_new(links, "last", json_string(last.c_str()));

                if (offset > 0 && offset * limit < total_size)
                {
                    auto prev = base + std::to_string(offset - 1);
                    json_object_set_new(links, "prev", json_string(prev.c_str()));
                }
            }

            json_t* meta = json_object_get(m_body, "meta");

            if (!meta)
            {
                json_object_set_new(m_body, "meta", json_object());
                meta = json_object_get(m_body, "meta");
            }

            json_object_set_new(meta, "total", json_integer(total_size));
        }
    }
}

std::string HttpResponse::to_string() const
{
    std::ostringstream ss;
    ss << "HTTP " << m_code << "\n";

    for (const auto& [key, value] : m_headers)
    {
        ss << key << ": " << value << "\n";
    }

    if (m_body)
    {
        ss << mxb::json_dump(m_body, JSON_INDENT(2));
    }

    return ss.str();
}
