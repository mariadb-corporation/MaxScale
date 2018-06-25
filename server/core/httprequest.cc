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

#include "internal/httprequest.hh"
#include "internal/admin.hh"

#include <ctype.h>
#include <string.h>
#include <sstream>

using std::string;
using std::deque;

#define HTTP_HOST_HEADER     "Host"
#define HTTP_METHOD_OVERRIDE "X-HTTP-Method-Override"

const std::string HttpRequest::HTTP_PREFIX = "http://";
const std::string HttpRequest::HTTPS_PREFIX = "https://";

static void process_uri(string& uri, std::deque<string>& uri_parts)
{
    /** Clean up trailing slashes in requested resource */
    while (uri.length() > 1 && *uri.rbegin() == '/')
    {
        uri.erase(uri.find_last_of("/"));
    }

    string my_uri = uri;

    while (my_uri.length() && *my_uri.begin() == '/')
    {
        my_uri.erase(my_uri.begin());
    }

    while (my_uri.length() > 0)
    {
        size_t pos = my_uri.find("/");
        string part = pos == string::npos ? my_uri : my_uri.substr(0, pos);
        my_uri.erase(0, pos == string::npos ? pos : pos + 1);
        uri_parts.push_back(part);
    }
}

HttpRequest::HttpRequest(struct MHD_Connection *connection, string url, string method, json_t* data):
    m_json(data),
    m_json_string(data ? mxs::json_dump(data, 0) : ""),
    m_resource(url),
    m_verb(method),
    m_connection(connection)
{
    process_uri(url, m_resource_parts);

    m_hostname = mxs_admin_https_enabled() ? HttpRequest::HTTPS_PREFIX : HttpRequest::HTTP_PREFIX;
    m_hostname += get_header(HTTP_HOST_HEADER);

    string method_override = get_header(HTTP_METHOD_OVERRIDE);

    if (method_override.length())
    {
        m_verb = method_override;
    }

    if (m_hostname[m_hostname.size() - 1] != '/')
    {
        m_hostname += "/";
    }

    m_hostname += MXS_REST_API_VERSION;
}

HttpRequest::~HttpRequest()
{
}

bool HttpRequest::validate_api_version()
{
    bool rval = false;

    if (m_resource_parts.size() > 0 &&
        m_resource_parts[0] == MXS_REST_API_VERSION)
    {
        m_resource_parts.pop_front();
        rval = true;
    }

    return rval;
}

namespace
{
struct ValueFormatter
{
    std::stringstream ss;
    const char* separator;
    const char* terminator;

    ValueFormatter(const char* sep, const char* term):
        separator(sep), terminator(term)
    {
    }
};
}

static int value_combine_cb(void *cls,
                            enum MHD_ValueKind kind,
                            const char *key,
                            const char *value)
{
    ValueFormatter& cnf = *(ValueFormatter*)cls;

    cnf.ss << key;

    if (value)
    {
        cnf.ss << cnf.separator << value;
    }

    cnf.ss << cnf.terminator;

    return MHD_YES;
}

std::string HttpRequest::to_string() const
{
    std::stringstream req;
    req << m_verb << " " << m_resource;

    ValueFormatter opts("=", "&");
    MHD_get_connection_values(m_connection, MHD_GET_ARGUMENT_KIND,
                              value_combine_cb, &opts);

    std::string optstr = opts.ss.str();
    size_t len = optstr.length();

    if (len)
    {
        req << "?";

        if (optstr[len - 1] == '&')
        {
            optstr.erase(len - 1);
        }
    }

    req << optstr << " " << "HTTP/1.1" << "\r\n";

    ValueFormatter hdr(": ", "\r\n");
    MHD_get_connection_values(m_connection, MHD_HEADER_KIND,
                              value_combine_cb, &hdr);

    std::string hdrstr = hdr.ss.str();

    if (hdrstr.length())
    {
        req << hdrstr;
    }

    req << "\r\n";

    req << m_json_string;
    return req.str();
}
