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

#include "maxscale/httpparser.hh"

#include <ctype.h>

static enum http_verb string_to_http_verb(string& verb)
{
    if (verb == "GET")
    {
        return HTTP_GET;
    }
    else if (verb == "POST")
    {
        return HTTP_POST;
    }
    else if (verb == "PUT")
    {
        return HTTP_PUT;
    }
    else if (verb == "PATCH")
    {
        return HTTP_PATCH;
    }
    else if (verb == "OPTIONS")
    {
        return HTTP_OPTIONS;
    }

    return HTTP_UNKNOWN;
}

HttpParser* HttpParser::parse(string request)
{
    size_t pos = request.find("\r\n");
    string request_line = request.substr(0, pos);
    request.erase(0, pos + 2);

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

    while ((pos = request.find("\r\n")) != string::npos)
    {
        string header_line = request.substr(0, pos);
        request.erase(0, pos + 2);

        if (header_line.length() == 0)
        {
            /** End of headers */
            break;
        }

        if ((pos = header_line.find(":")) != string::npos)
        {
            string key = header_line.substr(0, pos);
            header_line.erase(0, pos + 1);

            while (isspace(header_line[0]))
            {
                header_line.erase(0, 1);
            }

            headers[key] = header_line;
        }
    }

    HttpParser* parser = NULL;
    enum http_verb verb_value = string_to_http_verb(verb);

    if (http_version == "HTTP/1.1" && verb_value != HTTP_UNKNOWN)
    {
        parser = new HttpParser();
        parser->m_verb = verb_value;
        parser->m_resource = uri;
        parser->m_headers = headers;
        parser->m_body = request;
    }

    return parser;
}

HttpParser::HttpParser()
{

}

HttpParser::~HttpParser()
{

}
