/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/json.hh>

#include <string>

namespace maxscale
{

bool get_json_string(json_t* json, const char* ptr, std::string* out)
{
    auto val = mxb::json_ptr(json, ptr);
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
    auto val = mxb::json_ptr(json, ptr);
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
    auto val = mxb::json_ptr(json, ptr);
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
    auto val = mxb::json_ptr(json, ptr);
    bool rval = false;

    if (json_is_boolean(val))
    {
        *out = json_boolean_value(val);
        rval = true;
    }

    return rval;
}

}
