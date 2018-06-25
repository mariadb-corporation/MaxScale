#pragma once
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

#include <maxscale/cppdefs.hh>

#include <sstream>
#include <string>

#include <maxscale/alloc.h>
#include <maxscale/debug.h>
#include <maxscale/jansson.h>
#include <maxscale/utils.hh>

namespace maxscale
{

/**
 * @class CloserTraits<json_t*> jansson.hh <maxscale/jansson.hh>
 *
 * Specialization of @c CloserTraits for @c json_t*.
 */
template<>
struct CloserTraits<json_t*>
{
    static void close_if(json_t* pJson)
    {
        if (pJson)
        {
            json_decref(pJson);
        }
    }

    static void reset(json_t*& pJson)
    {
        pJson = NULL;
    }
};

/**
 * @brief Convenience function for dumping JSON into a string
 *
 * @param json JSON to dump
 *
 * @return The JSON in string format
 */
static inline std::string json_dump(const json_t* json, int flags = 0)
{
    std::string rval;
    char* js = json_dumps(json, flags);

    if (js)
    {
        rval = js;
        MXS_FREE(js);
    }

    return rval;
}

static inline std::string json_dump(const Closer<json_t*>& json, int flags = 0)
{
    return json_dump(json.get(), flags);
}

/**
 * @brief Convert JSON to string
 *
 * @param JSON to convert
 *
 * @return The JSON value converted to a string
 */
static inline std::string json_to_string(json_t* json)
{
    std::stringstream ss;

    switch (json_typeof(json))
    {
    case JSON_STRING:
        ss << json_string_value(json);
        break;

    case JSON_INTEGER:
        ss << json_integer_value(json);
        break;

    case JSON_REAL:
        ss << json_real_value(json);
        break;

    case JSON_TRUE:
        ss << "true";
        break;

    case JSON_FALSE:
        ss << "false";
        break;

    case JSON_NULL:
        break;

    default:
        ss_dassert(false);
        break;

    }

    return ss.str();
}
}
