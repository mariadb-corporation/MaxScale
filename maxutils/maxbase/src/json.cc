/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
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
const char key_not_found[] = "Key '%s' was not found in json data.";
const char val_is_null[] = "'%s' is null.";

std::string grab_next_component(std::string* s)
{
    std::string& str = *s;

    while (str.length() > 0 && str[0] == '/')
    {
        str.erase(str.begin());
    }

    size_t pos = str.find('/');
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

bool is_integer(const std::string& str)
{
    char* end;
    return strtol(str.c_str(), &end, 10) >= 0 && *end == '\0';
}

json_t* json_ptr_internal(const json_t* json, std::string str)
{
    json_t* rval = NULL;
    std::string comp = grab_next_component(&str);

    if (comp.length() == 0)
    {
        return const_cast<json_t*>(json);
    }

    if (json_is_array(json) && is_integer(comp))
    {
        size_t idx = strtol(comp.c_str(), NULL, 10);

        if (idx < json_array_size(json))
        {
            rval = json_ptr_internal(json_array_get(json, idx), str);
        }
    }
    else if (json_is_object(json))
    {
        json_t* obj = json_object_get(json, comp.c_str());

        if (obj)
        {
            rval = json_ptr_internal(obj, str);
        }
    }

    return rval;
}
}

namespace maxbase
{

bool Json::load_string(const string& source)
{
    json_error_t error;
    auto res = json_loads(source.c_str(), 0, &error);
    if (res)
    {
        reset(res);
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

std::string Json::get_string() const
{
    return json_is_string(m_obj) ? json_string_value(m_obj) : "";
}

std::string Json::get_string(const char* key) const
{
    string rval;
    json_t* obj = json_object_get(m_obj, key);
    if (obj)
    {
        if (json_is_string(obj))
        {
            rval = json_string_value(obj);
        }
        else
        {
            m_errormsg = mxb::string_printf("'%s' is a JSON %s, not a JSON string.",
                                            key, json_type_to_string(obj));
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


int64_t Json::get_int() const
{
    return json_is_integer(m_obj) ? json_integer_value(m_obj) : 0;
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
        else
        {
            m_errormsg = mxb::string_printf("'%s' is a JSON %s, not a JSON string.",
                                            key, json_type_to_string(obj));
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
            m_errormsg = mxb::string_printf("'%s' is a JSON %s, not a JSON array.",
                                            keyc, json_type_to_string(obj));
        }
    }
    else
    {
        m_errormsg = mxb::string_printf(key_not_found, keyc);
    }
    return rval;
}

std::vector<Json> Json::get_array_elems() const
{
    std::vector<Json> rval;

    if (type() == Type::ARRAY)
    {
        rval.reserve(json_array_size(m_obj));

        size_t index;
        json_t* elem;
        json_array_foreach(m_obj, index, elem)
        {
            rval.emplace_back(elem);
        }
    }

    return rval;
}

std::vector<std::string> Json::keys() const
{
    std::vector<std::string> rval;
    rval.reserve(json_object_size(m_obj));

    const char* key;
    json_t* ignored;
    json_object_foreach(m_obj, key, ignored)
    {
        rval.push_back(key);
    }

    return rval;
}

const std::string& Json::error_msg() const
{
    return m_errormsg;
}

bool Json::valid() const
{
    return m_obj;
}

bool Json::contains(const char* key) const
{
    return json_object_get(m_obj, key);
}

bool Json::contains(const string& key) const
{
    return contains(key.c_str());
}

Json::Type Json::type() const
{
    if (m_obj)
    {
        switch (json_typeof(m_obj))
        {
        case JSON_OBJECT:
            return Type::OBJECT;

        case JSON_ARRAY:
            return Type::ARRAY;

        case JSON_STRING:
            return Type::STRING;

        case JSON_INTEGER:
            return Type::INTEGER;

        case JSON_REAL:
            return Type::REAL;

        case JSON_TRUE:
        case JSON_FALSE:
            return Type::BOOL;

        case JSON_NULL:
            return Type::JSON_NULL;
        }
    }

    return Type::UNDEFINED;
}

bool Json::try_get_int(const std::string& key, int64_t* out) const
{
    bool rval = false;
    auto keyc = key.c_str();
    json_t* obj = json_object_get(m_obj, keyc);
    if (json_is_integer(obj))
    {
        *out = json_integer_value(obj);
        rval = true;
    }
    return rval;
}

bool Json::try_get_bool(const char* key, bool* out) const
{
    bool rval = false;
    json_t* obj = json_object_get(m_obj, key);
    if (json_is_boolean(obj))
    {
        *out = json_boolean_value(obj);
        rval = true;
    }
    return rval;
}


bool Json::try_get_bool(const string& key, bool* out) const
{
    return try_get_bool(key.c_str(), out);
}

bool Json::try_get_string(const char* key, std::string* out) const
{
    bool rval = false;
    json_t* obj = json_object_get(m_obj, key);
    if (json_is_string(obj))
    {
        *out = json_string_value(obj);
        rval = true;
    }
    return rval;
}

bool Json::try_get_string(const string& key, std::string* out) const
{
    return try_get_string(key.c_str(), out);
}

void Json::set_string(const char* key, const char* value)
{
    json_object_set_new(m_obj, key, json_string(value));
}

void Json::set_string(const char* key, const std::string& value)
{
    set_string(key, value.c_str());
}

void Json::set_int(const char* key, int64_t value)
{
    json_object_set_new(m_obj, key, json_integer(value));
}

void Json::set_float(const char* key, double value)
{
    json_object_set_new(m_obj, key, json_real(value));
}

void Json::set_bool(const char* key, bool value)
{
    json_object_set_new(m_obj, key, json_boolean(value));
}

void Json::set_null(const char* key)
{
    json_object_set_new(m_obj, key, json_null());
}

void Json::add_array_elem(const Json& elem)
{
    mxb_assert(json_is_array(m_obj));
    json_array_append(m_obj, elem.m_obj);
}

void Json::add_array_elem(Json&& elem)
{
    mxb_assert(json_is_array(m_obj));
    json_array_append_new(m_obj, elem.m_obj);
    elem.m_obj = nullptr;
}

void Json::set_object(const char* key, const Json& value)
{
    json_object_set(m_obj, key, value.m_obj);
}

void Json::set_object(const char* key, Json&& value)
{
    json_object_set_new(m_obj, key, value.m_obj);
    value.m_obj = nullptr;
}

bool Json::save(const std::string& filepath, Format format)
{
    int flags = static_cast<int>(format);
    bool write_ok = false;
    auto filepathc = filepath.c_str();
    if (json_dump_file(m_obj, filepathc, flags) == 0)
    {
        write_ok = true;
    }
    else
    {
        int eno = errno;
        m_errormsg = mxb::string_printf("Json write to file '%s' failed. Error %d, %s.",
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

    case Type::STRING:
    case Type::INTEGER:
    case Type::REAL:
    case Type::BOOL:
        // These are currently not useful (or required), as the contained value cannot be modified.
        mxb_assert(!true);
        break;

    case Type::JSON_NULL:
        m_obj = json_null();
        break;

    case Type::UNDEFINED:
        break;
    }
}

bool Json::load(const string& filepath)
{
    auto filepathc = filepath.c_str();
    json_error_t err;
    json_t* obj = json_load_file(filepathc, 0, &err);
    bool rval = false;
    if (obj)
    {
        reset(obj);
        rval = true;
    }
    else
    {
        m_errormsg = mxb::string_printf("Json read from file '%s' failed: %s", filepathc, err.text);
    }
    return rval;
}

void Json::erase(const char* key)
{
    json_object_del(m_obj, key);
}

void Json::erase(const std::string& key)
{
    erase(key.c_str());
}

void Json::reset(json_t* obj)
{
    json_decref(m_obj);
    m_obj = obj;
    m_errormsg.clear();
}

bool Json::equal(const Json& other) const
{
    return valid() == other.valid() && (!valid() || json_equal(m_obj, other.m_obj));
}

bool Json::ok() const
{
    return m_errormsg.empty();
}

json_t* Json::get_json() const
{
    return m_obj;
}

std::string Json::to_string(Format format) const
{
    return json_dump(m_obj, static_cast<int>(format));
}

Json Json::at(const char* ptr) const
{
    if (valid())
    {
        if (json_t* js = json_ptr(m_obj, ptr))
        {
            return Json(js);
        }
    }

    return Json(Type::UNDEFINED);
}

std::string json_dump(const json_t* json, int flags)
{
    std::string rval;

    auto dump_cb = [](const char* buffer, size_t size, void* data) {
            std::string* str = reinterpret_cast<std::string*>(data);
            str->append(buffer, size);
            return 0;
        };

    json_dump_callback(json, dump_cb, &rval, flags);
    return rval;
}

json_t* json_ptr(const json_t* json, const char* json_ptr)
{
    return json_ptr_internal(json, json_ptr);
}

const char* json_type_to_string(const json_t* json)
{
    switch (json_typeof(json))
    {
    case JSON_OBJECT:
        return "object";

    case JSON_ARRAY:
        return "array";

    case JSON_STRING:
        return "string";

    case JSON_INTEGER:
        return "integer";

    case JSON_REAL:
        return "real";

    case JSON_TRUE:
    case JSON_FALSE:
        return "boolean";

    case JSON_NULL:
        return "null";

    default:
        mxb_assert(!true);
    }

    return "unknown";
}
}
