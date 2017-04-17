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

static bool process_options(string& uri, map<string, string>& options)
{
    size_t pos = uri.find("?");

    if (pos != string::npos)
    {
        string optionstr = uri.substr(pos + 1);
        uri.erase(pos);

        char buf[optionstr.size() + 1];
        strcpy(buf, optionstr.c_str());
        char* saved;
        char* tok = strtok_r(buf, ",", &saved);

        while (tok && *tok)
        {
            string opt(tok);
            pos = opt.find("=");

            if (pos != string::npos)
            {
                string key = opt.substr(0, pos - 1);
                string value = opt.substr(pos + 1);
                options[key] = value;
            }
            else
            {
                /** Invalid option */
                return false;
            }

            tok = strtok_r(NULL, ",", &saved);
        }
    }

    return true;
}

static bool process_headers(string& data, map<string, string>& headers)
{
    size_t pos;

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
        else
        {
            /** Invalid header */
            return false;
        }
    }

    return true;
}

HttpRequest* HttpRequest::parse(string data)
{
    size_t pos = data.find("\r\n");

    if (pos == string::npos)
    {
        return NULL;
    }

    string request_line = data.substr(0, pos);
    data.erase(0, pos + 2);

    /** Request method */
    if ((pos = request_line.find(" ")) == string::npos)
    {
        return NULL;
    }

    string verb = request_line.substr(0, pos);
    request_line.erase(0, pos + 1);

    /** Get the combined URL/option string */
    if ((pos = request_line.find(" ")) == string::npos)
    {
        return NULL;
    }

    string uri = request_line.substr(0, pos);
    request_line.erase(0, pos + 1);

    map<string, string> options;

    /** Process request options */
    if (!process_options(uri, options))
    {
        return NULL;
    }

    /** Split the URI into separate parts */
    deque<string> uri_parts;
    process_uri(uri, uri_parts);

    pos = request_line.find("\r\n");
    string http_version = request_line.substr(0, pos);
    request_line.erase(0, pos + 2);


    map<string, string> headers;

    /** Process request headers */
    if (!process_headers(data, headers))
    {
        return NULL;
    }

    /**
     * The headers are now processed and consumed. The message body is
     * the only thing left in the request string and it should be a JSON object.
     * Attempt to parse it and return an error if it fails.
     */

    bool ok = false;
    HttpRequest* request = NULL;
    enum http_verb verb_value = string_to_http_verb(verb);
    json_error_t json_error = {};
    json_t* body = NULL;

    /** Remove leading and trailing whitespace */
    if (data.length())
    {
        mxs::trim(data);
    }

    if (http_version == "HTTP/1.1" && verb_value != HTTP_UNKNOWN)
    {
        if (data.length() && (body = json_loads(data.c_str(), 0, &json_error)) == NULL)
        {
            MXS_DEBUG("JSON error in input on line %d column %d: %s (%s)",
                      json_error.line, json_error.column, json_error.text,
                      data.c_str());
        }
        else
        {
            ok = true;
        }
    }

    if (ok)
    {
        request = new HttpRequest();
        request->m_headers = headers;
        request->m_options = options;
        request->m_json.reset(body);
        request->m_json_string = data;
        request->m_resource = uri;
        request->m_resource_parts = uri_parts;
        request->m_verb = verb_value;
    }

    return request;
}

HttpRequest::HttpRequest():
    m_json(NULL),
    m_verb(HTTP_UNKNOWN)
{

}

HttpRequest::~HttpRequest()
{

}
