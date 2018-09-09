/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>

#include <deque>
#include <map>
#include <string>
#include <memory>
#include <cstdint>
#include <microhttpd.h>

#include <maxscale/jansson.hh>
#include <maxscale/utils.hh>
#include <maxscale/http.hh>

// The API version part of the URL
#define MXS_REST_API_VERSION "v1"

static int value_iterator(void* cls,
                          enum MHD_ValueKind kind,
                          const char* key,
                          const char* value)
{
    std::pair<std::string, std::string>* cmp = (std::pair<std::string, std::string>*)cls;

    if (strcasecmp(cmp->first.c_str(), key) == 0 && value)
    {
        cmp->second = value;
        return MHD_NO;
    }

    return MHD_YES;
}

static int value_sum_iterator(void* cls,
                              enum MHD_ValueKind kind,
                              const char* key,
                              const char* value)
{
    size_t& count = *(size_t*)cls;
    count++;
    return MHD_YES;
}

static int value_copy_iterator(void* cls,
                               enum MHD_ValueKind kind,
                               const char* key,
                               const char* value)
{
    std::string k = key;
    if (value)
    {
        k += "=";
        k += value;
    }

    char**& dest = *(char***) cls;
    *dest = MXS_STRDUP_A(k.c_str());
    dest++;

    return MHD_YES;
}

class HttpRequest
{
    HttpRequest(const HttpRequest&);
    HttpRequest& operator=(const HttpRequest);
public:
    /**
     * @brief Parse a request
     *
     * @param request Request to parse
     *
     * @return Parsed statement or NULL if request is not valid
     */
    HttpRequest(struct MHD_Connection* connection, std::string url, std::string method, json_t* data);

    ~HttpRequest();

    /**
     * @brief Return request verb type
     *
     * @return One of the HTTP verb values
     */
    const std::string& get_verb() const
    {
        return m_verb;
    }

    /**
     * @brief Get header value
     *
     * @param header Header to get
     *
     * @return Header value or empty string if the header was not found
     */
    std::string get_header(const std::string& header) const
    {
        std::pair<std::string, std::string> p;
        p.first = header;

        MHD_get_connection_values(m_connection,
                                  MHD_HEADER_KIND,
                                  value_iterator,
                                  &p);

        return p.second;
    }

    /**
     * @brief Get option value
     *
     * @param header Option to get
     *
     * @return Option value or empty string if the option was not found
     */
    std::string get_option(const std::string& option) const
    {
        std::pair<std::string, std::string> p;
        p.first = option;

        MHD_get_connection_values(m_connection,
                                  MHD_GET_ARGUMENT_KIND,
                                  value_iterator,
                                  &p);

        return p.second;
    }

    /**
     * @brief Get request option count
     *
     * @return Number of options in the request
     */
    size_t get_option_count() const
    {
        size_t rval = 0;
        MHD_get_connection_values(m_connection,
                                  MHD_GET_ARGUMENT_KIND,
                                  value_sum_iterator,
                                  &rval);

        return rval;
    }

    /**
     * @brief Copy options to an array
     *
     * The @c dest parameter must be able to hold at least get_option_count()
     * pointers. The values stored need to be freed by the caller.
     *
     * @param dest Destination where options are copied
     */
    void copy_options(char** dest) const
    {
        MHD_get_connection_values(m_connection,
                                  MHD_GET_ARGUMENT_KIND,
                                  value_copy_iterator,
                                  &dest);
    }

    /**
     * @brief Return request body
     *
     * @return Request body or empty string if no body is defined
     */
    const std::string& get_json_str() const
    {
        return m_json_string;
    }

    /**
     * @brief Return raw JSON body
     *
     * @return Raw JSON body or NULL if no body is defined
     */
    json_t* get_json() const
    {
        return m_json.get();
    }

    /**
     * @brief Get complete request URI
     *
     * @return The complete request URI
     */
    std::string get_uri() const
    {
        return m_resource;
    }

    /**
     * @brief Get URI part
     *
     * @param idx Zero indexed part number in URI
     *
     * @return The request URI part or empty string if no part was found
     */
    std::string uri_part(uint32_t idx) const
    {
        return m_resource_parts.size() > idx ? m_resource_parts[idx] : "";
    }

    /**
     * @brief Return a segment of the URI
     *
     * Combines a range of parts into a segment of the URI. Each part is
     * separated by a forward slash.
     *
     * @param start Start of range
     * @param end   End of range, not inclusive
     *
     * @return The URI segment that matches this range
     */
    std::string uri_segment(uint32_t start, uint32_t end) const
    {
        std::string rval;

        for (uint32_t i = start; i < end && i < m_resource_parts.size(); i++)
        {
            if (i > start)
            {
                rval += "/";
            }

            rval += m_resource_parts[i];
        }

        return rval;
    }

    /**
     * @brief Return how many parts are in the URI
     *
     * @return Number of URI parts
     */
    size_t uri_part_count() const
    {
        return m_resource_parts.size();
    }

    /**
     * @brief Return the last part of the URI
     *
     * @return The last URI part
     */
    std::string last_uri_part() const
    {
        return m_resource_parts.size() > 0 ? m_resource_parts[m_resource_parts.size() - 1] : "";
    }

    /**
     * @brief Return the value of the Host header
     *
     * @return The value of the Host header
     */
    const char* host() const
    {
        return m_hostname.c_str();
    }

    /**
     * @brief Convert request to string format
     *
     * The returned string should be logically equivalent to the original request.
     *
     * @return The request in string format
     */
    std::string to_string() const;

    /**
     * @brief Drop the API version prefix
     *
     * @return True if prefix is present and was successfully removed
     */
    bool validate_api_version();

private:

    /** Constants */
    static const std::string HTTP_PREFIX;
    static const std::string HTTPS_PREFIX;

    std::map<std::string, std::string> m_options;       /**< Request options */
    std::unique_ptr<json_t>            m_json;          /**< Request body */
    std::string                        m_json_string;   /**< String version of @c m_json */
    std::string                        m_resource;      /**< Requested resource */
    std::deque<std::string>            m_resource_parts;/**< @c m_resource split into parts */
    std::string                        m_verb;          /**< Request method */
    std::string                        m_hostname;      /**< The value of the Host header */
    struct MHD_Connection*             m_connection;
};
