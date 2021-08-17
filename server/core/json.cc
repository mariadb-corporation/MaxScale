/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/json.hh>

#include <string>

static std::string grab_next_component(std::string* s)
{
    std::string& str = *s;

    while (str.length() > 0 && str[0] == '/')
    {
        str.erase(str.begin());
    }

    size_t pos = str.find("/");
    std::string rval;

    if (pos != std::string::npos)
    {
        rval = str.substr(0, pos);
        str.erase(0, pos);
        return rval;
    }
    else
    {
        rval = str;
        str.erase(0);
    }

    return rval;
}

static bool is_integer(const std::string& str)
{
    char* end;
    return strtol(str.c_str(), &end, 10) >= 0 && *end == '\0';
}

static json_t* mxs_json_pointer_internal(json_t* json, std::string str)
{
    json_t* rval = NULL;
    std::string comp = grab_next_component(&str);

    if (comp.length() == 0)
    {
        return json;
    }

    if (json_is_array(json) && is_integer(comp))
    {
        size_t idx = strtol(comp.c_str(), NULL, 10);

        if (idx < json_array_size(json))
        {
            rval = mxs_json_pointer_internal(json_array_get(json, idx), str);
        }
    }
    else if (json_is_object(json))
    {
        json_t* obj = json_object_get(json, comp.c_str());

        if (obj)
        {
            rval = mxs_json_pointer_internal(obj, str);
        }
    }

    return rval;
}

json_t* mxs_json_pointer(json_t* json, const char* json_ptr)
{
    return mxs_json_pointer_internal(json, json_ptr);
}

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
}
