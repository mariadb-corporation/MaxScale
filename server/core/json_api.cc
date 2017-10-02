/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/json_api.h>

#include <string>

#include <maxscale/config.h>
#include <jansson.h>

using std::string;

namespace
{

const char DETAIL[] = "detail";
const char ERRORS[] = "errors";

}

static json_t* self_link(const char* host, const char* endpoint)
{
    json_t* self_link = json_object();
    string links = host;
    links += endpoint;
    json_object_set_new(self_link, CN_SELF, json_string(links.c_str()));

    return self_link;
}

json_t* mxs_json_resource(const char* host, const char* self, json_t* data)
{
    ss_dassert(data && (json_is_array(data) || json_is_object(data) || json_is_null(data)));
    json_t* rval = json_object();
    json_object_set_new(rval, CN_LINKS, self_link(host, self));
    json_object_set_new(rval, CN_DATA, data);
    return rval;
}

json_t* mxs_json_metadata(const char* host, const char* self, json_t* data)
{
    json_t* rval = json_object();
    json_object_set_new(rval, CN_LINKS, self_link(host, self));
    json_object_set_new(rval, CN_META, data);
    return rval;
}

json_t* mxs_json_relationship(const char* host, const char* endpoint)
{
    json_t* rel = json_object();

    /** Add the relation self link */
    json_object_set_new(rel, CN_LINKS, self_link(host, endpoint));

    /** Add empty array of relations */
    json_object_set_new(rel, CN_DATA, json_array());
    return rel;
}

void mxs_json_add_relation(json_t* rel, const char* id, const char* type)
{
    json_t* data = json_object_get(rel, CN_DATA);
    ss_dassert(data && json_is_array(data));

    json_t* obj = json_object();
    json_object_set_new(obj, CN_ID, json_string(id));
    json_object_set_new(obj, CN_TYPE, json_string(type));
    json_array_append_new(data, obj);
}

static string grab_next_component(string* s)
{
    std::string& str = *s;

    while (str.length() > 0 && str[0] == '/')
    {
        str.erase(str.begin());
    }

    size_t pos = str.find("/");
    string rval;

    if (pos != string::npos)
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

static bool is_integer(const string& str)
{
    char* end;
    return strtol(str.c_str(), &end, 10) >= 0 && *end == '\0';
}

static json_t* mxs_json_pointer_internal(json_t* json, string str)
{
    json_t* rval = NULL;
    string comp = grab_next_component(&str);

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

static json_t* json_error_append(json_t* obj, const char* message)
{
    json_t* err = json_error_detail(message);

    json_t* arr = json_object_get(obj, ERRORS);
    ss_dassert(arr);
    ss_dassert(json_is_array(arr));

    if (arr)
    {
        json_array_append_new(arr, err);
    }

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
