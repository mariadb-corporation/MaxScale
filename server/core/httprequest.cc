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

#include "maxscale/httprequest.hh"

#include <ctype.h>
#include <string.h>

/** TODO: Move this to a C++ string utility header */
namespace maxscale
{
static inline string& trim(string& str)
{
    if (str.length())
    {
        if (isspace(*str.begin()))
        {
            string::iterator it = str.begin();

            while (it != str.end() && isspace(*it))
            {
                it++;
            }
            str.erase(str.begin(), it);
        }

        if (isspace(*str.rbegin()))
        {
            string::reverse_iterator it = str.rbegin();
            while (it != str.rend() && isspace(*it))
            {
                it++;
            }

            str.erase(it.base(), str.end());
        }
    }

    return str;
}
}

static void process_uri(string& uri, deque<string>& uri_parts)
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

    if (my_uri.length() == 0)
    {
        /** Special handling for the / resource */
        uri_parts.push_back("");
    }
    else
    {
        while (my_uri.length() > 0)
        {
            size_t pos = my_uri.find("/");
            string part = pos == string::npos ? my_uri : my_uri.substr(0, pos);
            my_uri.erase(0, pos == string::npos ? pos : pos + 1);
            uri_parts.push_back(part);
        }
    }
}

HttpRequest::HttpRequest(struct MHD_Connection *connection, string url, string method, json_t* data):
    m_json(data),
    m_resource(url),
    m_verb(method),
    m_connection(connection)
{
    process_uri(url, m_resource_parts);
    m_hostname = get_header("Host");
}

HttpRequest::~HttpRequest()
{

}
