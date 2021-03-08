/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/json_api.hh>

#include <string>
#include <jansson.h>

#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>

#include "internal/monitormanager.hh"
#include "internal/filter.hh"
#include "internal/listener.hh"

using std::string;
using namespace std::literals::string_literals;

namespace
{

const char CN_META[] = "meta";
const char CN_SELF[] = "self";
const char CN_RELATED[] = "related";

const char DETAIL[] = "detail";
const char ERRORS[] = "errors";

bool target_validator(const char* str)
{
    return mxs::Target::find(str);
}

bool monitor_validator(const char* str)
{
    return MonitorManager::find_monitor(str);
}

bool filter_validator(const char* str)
{
    return filter_find(str).get();
}

bool listener_validator(const char* str)
{
    return listener_find(str).get();
}

std::unordered_map<std::string, std::function<bool(const char*)>> valid_relationships =
{
    {"servers",   target_validator  },
    {"services",  target_validator  },
    {"monitors",  monitor_validator },
    {"filters",   filter_validator  },
    {"listeners", listener_validator}
};

std::string validate_relationships(json_t* json)
{
    if (auto rel = mxs_json_pointer(json, MXS_JSON_PTR_RELATIONSHIPS))
    {
        if (!json_is_object(rel))
        {
            return "Field '"s + MXS_JSON_PTR_RELATIONSHIPS + "' is not an object";
        }

        const char* key;
        json_t* j;

        json_object_foreach(rel, key, j)
        {
            std::string path = MXS_JSON_PTR_RELATIONSHIPS + "/"s + key;
            json_t* arr = json_object_get(j, "data");

            if (!json_is_object(j))
            {
                return "Field '" + path + "' is not an object";
            }
            else if (valid_relationships.count(key) == 0)
            {
                return "'"s + key + "' is not a valid MaxScale relationship type";
            }
            else if (!json_is_array(arr) && !json_is_null(arr))
            {
                return "Field '" + path + "/data' is not an array";
            }

            size_t i;
            json_t* value;

            // If the arr is a JSON null, it won't be iterated
            json_array_foreach(arr, i, value)
            {
                auto relpath = path + "/" + std::to_string(i);

                if (!json_is_object(value))
                {
                    return "Field '" + relpath + "' is not an object";
                }
                else if (!json_is_string(json_object_get(value, "id")))
                {
                    return "Field '" + relpath + "/id' is not a string";
                }
                else if (!json_is_string(json_object_get(value, "type")))
                {
                    return "Field '" + relpath + "/type' is not a string";
                }
                else
                {
                    const char* name = json_string_value(json_object_get(value, "id"));

                    if (!valid_relationships[key](name))
                    {
                        return "'"s + name + "' is not a valid object of type '" + key + "'";
                    }
                }
            }
        }
    }

    return "";
}
}

static json_t* self_link(const std::string& host, const std::string& self, const std::string& related = "")
{
    json_t* self_link = json_object();
    string links = host + self;
    json_object_set_new(self_link, CN_SELF, json_string(links.c_str()));

    if (!related.empty())
    {
        string rel = host + related;
        json_object_set_new(self_link, CN_RELATED, json_string(rel.c_str()));
    }

    return self_link;
}

json_t* mxs_json_resource(const char* host, const char* self, json_t* data)
{
    mxb_assert(data && (json_is_array(data) || json_is_object(data) || json_is_null(data)));
    json_t* rval = json_object();
    json_object_set_new(rval, CN_LINKS, self_link(host, self));
    json_object_set_new(rval, CN_DATA, data);
    return rval;
}

std::string mxs_is_valid_json_resource(json_t* json)
{
    if (!json_is_object(mxs_json_pointer(json, MXS_JSON_PTR_DATA)))
    {
        return "The '"s + MXS_JSON_PTR_DATA + "' field is not an object";
    }

    for (auto a : {MXS_JSON_PTR_ID, MXS_JSON_PTR_TYPE})
    {
        if (!mxs_json_is_type(json, a, JSON_STRING))
        {
            return "The '"s + a + "' field is not a string";
        }
    }

    if (auto id = mxs_json_pointer(json, MXS_JSON_PTR_ID))
    {
        std::string reason;

        if (!config_is_valid_name(json_string_value(id), &reason))
        {
            return reason;
        }
    }

    if (auto parameters = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS))
    {
        if (!json_is_object(parameters))
        {
            return "The '"s + MXS_JSON_PTR_PARAMETERS "' field is not an object";
        }

        const char* key;
        json_t* value;

        json_object_foreach(parameters, key, value)
        {
            if (json_is_string(value) && strchr(json_string_value(value), '\n'))
            {
                return "Parameter '"s + key + "' contains unescaped newlines";
            }
        }
    }

    return validate_relationships(json);
}

json_t* mxs_json_metadata(const char* host, const char* self, json_t* data)
{
    json_t* rval = json_object();
    json_object_set_new(rval, CN_LINKS, self_link(host, self));
    json_object_set_new(rval, CN_META, data);
    return rval;
}

json_t* mxs_json_relationship(const std::string& host, const std::string& self, const std::string& related)
{
    json_t* rel = json_object();

    /** Add the relation self link */
    json_object_set_new(rel, CN_LINKS, self_link(host, self, related));

    /** Add empty array of relations */
    json_object_set_new(rel, CN_DATA, json_array());
    return rel;
}

void mxs_json_add_relation(json_t* rel, const char* id, const char* type)
{
    json_t* data = json_object_get(rel, CN_DATA);
    mxb_assert(data && json_is_array(data));

    json_t* obj = json_object();
    json_object_set_new(obj, CN_ID, json_string(id));
    json_object_set_new(obj, CN_TYPE, json_string(type));
    json_array_append_new(data, obj);
}

json_t* mxs_json_self_link(const char* host, const char* path, const char* id)
{
    json_t* links = json_object();

    string self = host;

    if (path[0] != '/')
    {
        self += "/";
    }

    self += path;

    if (self[self.length() - 1] != '/')
    {
        self += "/";
    }

    self += id;
    json_object_set_new(links, CN_SELF, json_string(self.c_str()));

    return links;
}

static json_t* json_error_detail(const char* message)
{
    json_t* err = json_object();
    json_object_set_new(err, DETAIL, json_string(message));
    return err;
}

static json_t* json_error(const char* message)
{
    json_t* err = json_error_detail(message);

    json_t* arr = json_array();
    json_array_append_new(arr, err);

    json_t* obj = json_object();
    json_object_set_new(obj, ERRORS, arr);

    return obj;
}

json_t* mxs_json_error(const char* format, ...)
{
    va_list args;

    va_start(args, format);
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    char message[len + 1];
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    return json_error(message);
}

json_t* mxs_json_error(const std::vector<std::string>& errors)
{
    json_t* rval = nullptr;

    if (!errors.empty())
    {
        auto it = errors.begin();
        rval = json_error(it->c_str());

        for (it = std::next(it); it != errors.end(); ++it)
        {
            rval = mxs_json_error_append(rval, "%s", it->c_str());
        }
    }

    return rval;
}

static json_t* json_error_append(json_t* obj, const char* message)
{
    json_t* err = json_error_detail(message);

    json_t* arr = json_object_get(obj, ERRORS);

    if (!arr)
    {
        arr = json_array();
        json_object_set_new(obj, ERRORS, arr);
    }
    else
    {
        mxb_assert(json_is_array(arr));
    }

    json_array_append_new(arr, err);

    return obj;
}

json_t* mxs_json_error_append(json_t* obj, const char* format, ...)
{
    va_list args;

    va_start(args, format);
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    char message[len + 1];
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (!obj)
    {
        obj = json_error(message);
    }
    else
    {
        obj = json_error_append(obj, message);
    }

    return obj;
}

namespace
{
enum class Location
{
    BACK,
    FRONT
};

json_t* json_error_insert_new(json_t* obj, json_t* err, Location location)
{
    if (!obj)
    {
        obj = json_object();
    }

    mxb_assert(obj);
    json_t* arr = json_object_get(obj, ERRORS);

    if (!arr)
    {
        arr = json_array();
        json_object_set_new(obj, ERRORS, arr);
    }

    mxb_assert(arr);
    mxb_assert(json_is_array(arr));

    if (location == Location::BACK)
    {
        json_array_append_new(arr, err);
    }
    else
    {
        json_array_insert_new(arr, 0, err);
    }

    return obj;
}
}

json_t* mxs_json_error_push_back(json_t* obj, json_t* err)
{
    json_incref(err);
    return mxs_json_error_push_back_new(obj, err);
}

json_t* mxs_json_error_push_back_new(json_t* obj, json_t* err)
{
    return json_error_insert_new(obj, err, Location::BACK);
}

json_t* mxs_json_error_push_front(json_t* obj, json_t* err)
{
    json_incref(err);
    return mxs_json_error_push_front_new(obj, err);
}

json_t* mxs_json_error_push_front_new(json_t* obj, json_t* err)
{
    return json_error_insert_new(obj, err, Location::FRONT);
}
