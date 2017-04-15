#pragma once
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

#include <maxscale/cppdefs.hh>

#include <string>
#include <map>
#include <tr1/memory>

using std::shared_ptr;
using std::string;
using std::map;

class HttpParser;

/** Typedef for managed pointer */
typedef std::shared_ptr<HttpParser> SHttpParser;

enum http_verb
{
    HTTP_UNKNOWN,
    HTTP_GET,
    HTTP_PUT,
    HTTP_POST,
    HTTP_OPTIONS,
    HTTP_PATCH
};

class HttpParser
{
public:
    /**
     * @brief Parse a request
     *
     * @param request Request to parse
     *
     * @return Parsed statement or NULL if request is not valid
     */
    static HttpParser* parse(string request);

    ~HttpParser();

    /**
     * @brief Return request verb type
     *
     * @return One of the HTTP verb values
     */
    enum http_verb get_verb() const
    {
        return m_verb;
    }

    /**
     * @brief Check if a request contains the specified header
     *
     * @param header Header to check
     *
     * @return True if header is in the request
     */
    bool have_header(const string& header) const
    {
        return m_headers.find(header) != m_headers.end();
    }

    /**
     * @brief Get header value
     *
     * @param header Header to get
     *
     * @return String value or empty string if no header found
     */
    const string get_header(const string header)
    {
        string rval;
        map<string, string>::iterator it = m_headers.find(header);

        if (it != m_headers.end())
        {
            rval = it->second;
        }

        return rval;
    }

    /**
     * @brief Check if body is defined
     *
     * @return True if body is defined
     */
    bool have_body() const
    {
        return m_body.length() != 0;
    }

    /**
     * @brief Return request body
     *
     * @return Request body or empty string if no body is defined
     */
    const string& get_body() const
    {
        return m_body;
    }

    void set_body(string body)
    {
        m_body = body;
    }

    /**
     * @brief Get request resource
     *
     * @return The request resoure
     */
    const string& get_resource() const
    {
        return m_resource;
    }

private:
    HttpParser();
    HttpParser(const HttpParser&);
    HttpParser& operator = (const HttpParser&);

    map<string, string> m_headers;
    string              m_body;
    string              m_resource;
    enum http_verb      m_verb;
};
