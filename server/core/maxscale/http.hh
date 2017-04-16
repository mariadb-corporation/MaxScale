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

#include <maxscale/debug.h>

using std::string;

/** Supported HTTP verbs */
enum http_verb
{
    HTTP_UNKNOWN,
    HTTP_GET,
    HTTP_PUT,
    HTTP_POST,
    HTTP_OPTIONS,
    HTTP_PATCH
};

/** Possible HTTP return codes */
enum http_code
{
    HTTP_200_OK,
    HTTP_201_CREATED,
    HTTP_202_ACCEPTED,
    HTTP_204_NO_CONTENT,
    HTTP_301_MOVED_PERMANENTLY,
    HTTP_302_FOUND,
    HTTP_303_SEE_OTHER,
    HTTP_304_NOT_MODIFIED,
    HTTP_307_TEMPORARY_REDIRECT,
    HTTP_308_PERMANENT_REDIRECT,
    HTTP_400_BAD_REQUEST,
    HTTP_401_UNAUTHORIZED,
    HTTP_403_FORBIDDEN,
    HTTP_404_NOT_FOUND,
    HTTP_405_METHOD_NOT_ALLOWED,
    HTTP_406_NOT_ACCEPTABLE,
    HTTP_409_CONFLICT,
    HTTP_411_LENGTH_REQUIRED,
    HTTP_412_PRECONDITION_FAILED,
    HTTP_413_PAYLOAD_TOO_LARGE,
    HTTP_414_URI_TOO_LONG,
    HTTP_415_UNSUPPORTED_MEDIA_TYPE,
    HTTP_422_UNPROCESSABLE_ENTITY,
    HTTP_423_LOCKED,
    HTTP_428_PRECONDITION_REQUIRED,
    HTTP_431_REQUEST_HEADER_FIELDS_TOO_LARGE,
    HTTP_500_INTERNAL_SERVER_ERROR,
    HTTP_501_NOT_IMPLEMENTED,
    HTTP_502_BAD_GATEWAY,
    HTTP_503_SERVICE_UNAVAILABLE,
    HTTP_504_GATEWAY_TIMEOUT,
    HTTP_505_HTTP_VERSION_NOT_SUPPORTED,
    HTTP_506_VARIANT_ALSO_NEGOTIATES,
    HTTP_507_INSUFFICIENT_STORAGE,
    HTTP_508_LOOP_DETECTED,
    HTTP_510_NOT_EXTENDED
} ;

/**
 * @brief Convert string to HTTP verb
 *
 * @param verb String containing HTTP verb
 *
 * @return Enum value of the verb
 */
static inline enum http_verb string_to_http_verb(string& verb)
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

/**
 * @brief Convert HTTP verb enum to string
 *
 * @param verb Enum to convert
 *
 * @return String representation of the enum
 */
static inline const char* http_verb_to_string(enum http_verb verb)
{
    switch (verb)
    {
    case HTTP_GET:
        return "GET";
    case HTTP_POST:
        return "POST";
    case HTTP_PUT:
        return "PUT";
    case HTTP_PATCH:
        return "PATCH";
    case HTTP_OPTIONS:
        return "OPTIONS";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Convert HTTP code to string
 *
 * @param code The code to convert
 *
 * @return The HTTP/1.1 string version of the code
 */
static inline const char* http_code_to_string(enum http_code code)
{
    switch (code)
    {
    case HTTP_200_OK:
        return "200 OK";
    case HTTP_201_CREATED:
        return "201 Created";
    case HTTP_202_ACCEPTED:
        return "202 Accepted";
    case HTTP_204_NO_CONTENT:
        return "204 No Content";
    case HTTP_301_MOVED_PERMANENTLY:
        return "301 Moved Permanently";
    case HTTP_302_FOUND:
        return "302 Found";
    case HTTP_303_SEE_OTHER:
        return "303 See Other";
    case HTTP_304_NOT_MODIFIED:
        return "304 Not Modified";
    case HTTP_307_TEMPORARY_REDIRECT:
        return "307 Temporary Redirect";
    case HTTP_308_PERMANENT_REDIRECT:
        return "308 Permanent Redirect";
    case HTTP_400_BAD_REQUEST:
        return "400 Bad Request";
    case HTTP_401_UNAUTHORIZED:
        return "401 Unauthorized";
    case HTTP_403_FORBIDDEN:
        return "403 Forbidden";
    case HTTP_404_NOT_FOUND:
        return "404 Not Found";
    case HTTP_405_METHOD_NOT_ALLOWED:
        return "405 Method Not Allowed";
    case HTTP_406_NOT_ACCEPTABLE:
        return "406 Not Acceptable";
    case HTTP_409_CONFLICT:
        return "409 Conflict";
    case HTTP_411_LENGTH_REQUIRED:
        return "411 Length Required";
    case HTTP_412_PRECONDITION_FAILED:
        return "412 Precondition Failed";
    case HTTP_413_PAYLOAD_TOO_LARGE:
        return "413 Payload Too Large";
    case HTTP_414_URI_TOO_LONG:
        return "414 URI Too Long";
    case HTTP_415_UNSUPPORTED_MEDIA_TYPE:
        return "415 Unsupported Media Type";
    case HTTP_422_UNPROCESSABLE_ENTITY:
        return "422 Unprocessable Entity";
    case HTTP_423_LOCKED:
        return "423 Locked";
    case HTTP_428_PRECONDITION_REQUIRED:
        return "428 Precondition Required";
    case HTTP_431_REQUEST_HEADER_FIELDS_TOO_LARGE:
        return "431 Request Header Fields Too Large";
    case HTTP_500_INTERNAL_SERVER_ERROR:
        return "500 Internal Server Error";
    case HTTP_501_NOT_IMPLEMENTED:
        return "501 Not Implemented";
    case HTTP_502_BAD_GATEWAY:
        return "502 Bad Gateway";
    case HTTP_503_SERVICE_UNAVAILABLE:
        return "503 Service Unavailable";
    case HTTP_504_GATEWAY_TIMEOUT:
        return "504 Gateway Timeout";
    case HTTP_505_HTTP_VERSION_NOT_SUPPORTED:
        return "505 HTTP Version Not Supported";
    case HTTP_506_VARIANT_ALSO_NEGOTIATES:
        return "506 Variant Also Negotiates";
    case HTTP_507_INSUFFICIENT_STORAGE:
        return "507 Insufficient Storage";
    case HTTP_508_LOOP_DETECTED:
        return "508 Loop Detected";
    case HTTP_510_NOT_EXTENDED:
        return "510 Not Extended";
    default:
        ss_dassert(false);
        return "500 Internal Server Error";
    }
}
