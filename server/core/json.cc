/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/json.hh>

#include <string>

bool mxs_json_is_type(json_t* json, const char* json_ptr, json_type type)
{
    bool rval = true;

    if (auto j = mxs_json_pointer(json, json_ptr))
    {
        rval = json_typeof(j) == type;
    }

    return rval;
}

namespace maxscale
{

bool get_json_string(json_t* json, const char* ptr, std::string* out)
{
    auto val = mxs_json_pointer(json, ptr);
    bool rval = false;

    if (json_is_string(val))
    {
        *out = json_string_value(val);
        rval = true;
    }

    return rval;
}

bool get_json_int(json_t* json, const char* ptr, int64_t* out)
{
    auto val = mxs_json_pointer(json, ptr);
    bool rval = false;

    if (json_is_integer(val))
    {
        *out = json_integer_value(val);
        rval = true;
    }

    return rval;
}

bool get_json_float(json_t* json, const char* ptr, double* out)
{
    auto val = mxs_json_pointer(json, ptr);
    bool rval = false;

    if (json_is_real(val))
    {
        *out = json_real_value(val);
        rval = true;
    }

    return rval;
}

bool get_json_bool(json_t* json, const char* ptr, bool* out)
{
    auto val = mxs_json_pointer(json, ptr);
    bool rval = false;

    if (json_is_boolean(val))
    {
        *out = json_boolean_value(val);
        rval = true;
    }

    return rval;
}

void json_remove_nulls(json_t* json)
{
    const char* key;
    json_t* value;
    void* tmp;

    json_object_foreach_safe(json, tmp, key, value)
    {
        if (json_is_null(value))
        {
            json_object_del(json, key);
        }
    }
}

void json_merge(json_t* dest, json_t* src)
{
    mxs::json_remove_nulls(dest);
    mxs::json_remove_nulls(src);
    json_object_update(dest, src);
}
}
