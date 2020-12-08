/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxbase/json.hh>
#include <maxbase/assert.h>
#include <maxbase/format.hh>
#include <maxbase/string.hh>
#include <jansson.h>

using std::string;

namespace
{
const char key_not_found[] = "Key %s was not found in json data.";
const char val_is_null[] = "%s is null.";
}

namespace maxbase
{

bool Json::load_string(const string& source)
{
    json_decref(m_obj);
    m_obj = nullptr;

    json_error_t error;
    auto res = json_loads(source.c_str(), 0, &error);
    if (res)
    {
        m_obj = res;
        m_errormsg.clear();
    }
    else
    {
        m_errormsg = error.text;
    }
    return res;
}

Json::Json(json_t* obj)
    : m_obj(obj)
{
    json_incref(m_obj);
}

Json::~Json()
{
    json_decref(m_obj);
}

Json::Json(const Json& rhs)
    : m_obj(rhs.m_obj)
{
    json_incref(m_obj);
}

void Json::swap(Json& rhs) noexcept
{
    std::swap(m_obj, rhs.m_obj);
    std::swap(m_errormsg, rhs.m_errormsg);
}

Json& Json::operator=(const Json& rhs)
{
    Json tmp(rhs);
    swap(tmp);
    return *this;
}

Json::Json(Json&& rhs) noexcept
    : m_obj(rhs.m_obj)
{
    rhs.m_obj = nullptr;
}

Json& Json::operator=(Json&& rhs)
{
    Json tmp(std::move(rhs));
    swap(tmp);
    return *this;
}

std::string Json::get_string(const char* key) const
{
    string rval;
    json_t* obj = json_object_get(m_obj, key);
    if (obj)
    {
        const char* val = json_string_value(obj);
        if (val)
        {
            rval = val;
        }
        else
        {
            if (json_is_null(obj))
            {
                m_errormsg = mxb::string_printf(val_is_null, key);
            }
            else
            {
                m_errormsg = mxb::string_printf("%s is not a json string", key);
            }
        }
    }
    else
    {
        m_errormsg = mxb::string_printf(key_not_found, key);
    }
    return rval;
}

std::string Json::get_string(const string& key) const
{
    return get_string(key.c_str());
}


int64_t Json::get_int(const char* key) const
{
    int64_t rval = 0;
    json_t* obj = json_object_get(m_obj, key);
    if (obj)
    {
        if (json_is_integer(obj))
        {
            rval = json_integer_value(obj);
        }
        else if (json_is_null(obj))
        {
            m_errormsg = mxb::string_printf(val_is_null, key);
        }
        else
        {
            m_errormsg = mxb::string_printf("%s is not a json integer", key);
        }
    }
    else
    {
        m_errormsg = mxb::string_printf(key_not_found, key);
    }
    return rval;
}

int64_t Json::get_int(const string& key) const
{
    return get_int(key.c_str());
}

Json Json::get_object(const char* key) const
{
    json_t* obj = json_object_get(m_obj, key);
    if (!obj)
    {
        m_errormsg = mxb::string_printf(key_not_found, key);
    }
    return Json(obj);
}

Json Json::get_object(const string& key) const
{
    return get_object(key.c_str());
}

std::vector<Json> Json::get_array_elems(const string& key) const
{
    std::vector<Json> rval;
    auto keyc = key.c_str();
    json_t* obj = json_object_get(m_obj, keyc);

    if (obj)
    {
        if (json_is_array(obj))
        {
            rval.reserve(json_array_size(obj));

            size_t index;
            json_t* elem;
            json_array_foreach(obj, index, elem)
            {
                rval.emplace_back(elem);
            }
        }
        else
        {
            m_errormsg = mxb::string_printf("%s is not a json array", keyc);
        }
    }
    else
    {
        m_errormsg = mxb::string_printf(key_not_found, keyc);
    }
    return rval;
}

std::string Json::error_msg() const
{
    return m_errormsg;
}

bool Json::valid() const
{
    return m_obj;
}

bool Json::contains(const string& key) const
{
    return json_object_get(m_obj, key.c_str());
}

bool Json::is_null(const string& key) const
{
    bool rval = false;
    auto keyc = key.c_str();
    json_t* obj = json_object_get(m_obj, keyc);

    if (obj)
    {
        rval = json_is_null(obj);
    }
    else
    {
        m_errormsg = mxb::string_printf(key_not_found, keyc);
    }
    return rval;
}

bool Json::try_get_int(const std::string& key, int64_t* out) const
{
    bool rval = false;
    auto keyc = key.c_str();
    json_t* obj = json_object_get(m_obj, keyc);
    if (obj && json_is_integer(obj))
    {
        *out = json_integer_value(obj);
        rval = true;
    }
    return rval;
}

void Json::set_string(const char* key, const char* value)
{
    json_object_set_new(m_obj, key, json_string(value));
}

void Json::set_int(const char* key, int64_t value)
{
    json_object_set_new(m_obj, key, json_integer(value));
}

void Json::add_array_elem(Json&& elem)
{
    mxb_assert(json_is_array(m_obj));
    json_array_append_new(m_obj, elem.m_obj);
    elem.m_obj = nullptr;
}

void Json::set_object(const char* key, Json&& value)
{
    json_object_set_new(m_obj, key, value.m_obj);
    value.m_obj = nullptr;
}

bool Json::save(const std::string& filepath)
{
    bool write_ok = false;
    auto filepathc = filepath.c_str();
    if (json_dump_file(m_obj, filepathc, JSON_INDENT(4)) == 0)
    {
        write_ok = true;
    }
    else
    {
        int eno = errno;
        m_errormsg = mxb::string_printf("Json write to file '%s' failed. Error %d, %s.\n",
                                        filepathc, eno, mxb_strerror(eno));
    }
    return write_ok;
}

Json::Json(Type type)
{
    switch (type)
    {
    case Type::OBJECT:
        m_obj = json_object();
        break;

    case Type::ARRAY:
        m_obj = json_array();
        break;

    case Type::JS_NULL:
        m_obj = json_null();
        break;
    }
}

bool Json::load(const string& filepath)
{
    reset();
    auto filepathc = filepath.c_str();
    json_error_t err;
    json_t* obj = json_load_file(filepathc, 0, &err);
    if (obj)
    {
        m_obj = obj;
    }
    else
    {
        m_errormsg = mxb::string_printf("Json read from file '%s' failed: %s", filepathc, err.text);
    }
    return false;
}

void Json::reset()
{
    json_decref(m_obj);
    m_obj = json_null();
    m_errormsg.clear();
}
}
