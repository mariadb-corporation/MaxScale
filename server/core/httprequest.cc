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

/** TODO: Move this to a C++ string utility header */
namespace maxscale
{
static inline string& trim(string& str)
{
    while (isspace(*str.begin()))
    {
        str.erase(str.begin());
    }

    while (isspace(*str.rbegin()))
    {
        str.erase(str.rbegin().base());
    }

    return str;
}
}

HttpRequest* HttpRequest::parse(string data)
{
    size_t pos = data.find("\r\n");
    string request_line = data.substr(0, pos);
    data.erase(0, pos + 2);

    pos = request_line.find(" ");
    string verb = request_line.substr(0, pos);
    request_line.erase(0, pos + 1);

    pos = request_line.find(" ");
    string uri = request_line.substr(0, pos);
    request_line.erase(0, pos + 1);

    pos = request_line.find("\r\n");
    string http_version = request_line.substr(0, pos);
    request_line.erase(0, pos + 2);

    map<string, string> headers;

    while ((pos = data.find("\r\n")) != string::npos)
    {
        string header_line = data.substr(0, pos);
        data.erase(0, pos + 2);

        if (header_line.length() == 0)
        {
            /** End of headers */
            break;
        }

        if ((pos = header_line.find(":")) != string::npos)
        {
            string key = header_line.substr(0, pos);
            header_line.erase(0, pos + 1);
            headers[key] = mxs::trim(header_line);
        }
    }

    /** The headers are now processed and consumed. The message body is
     * the only thing left in the request string. */

    HttpRequest* request = NULL;
    enum http_verb verb_value = string_to_http_verb(verb);

    if (http_version == "HTTP/1.1" && verb_value != HTTP_UNKNOWN)
    {
        request = new HttpRequest();
        request->m_verb = verb_value;
        request->m_resource = uri;
        request->m_headers = headers;
        request->m_body = data;
    }

    return request;
}

HttpRequest::HttpRequest()
{

}

HttpRequest::~HttpRequest()
{

}
