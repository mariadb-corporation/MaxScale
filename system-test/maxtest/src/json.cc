#include <maxtest/json.hh>

#include <maxbase/format.hh>
#include <jansson.h>

using std::string;

namespace
{
const char key_not_found[] = "Key %s was not found in json data.";
const char val_is_null[] = "%s is null.";
}

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

Json& Json::operator=(const Json& rhs)
{
    json_decref(m_obj);
    m_obj = rhs.m_obj;
    json_incref(m_obj);
    m_errormsg.clear();
    return *this;
}

Json::Json(Json&& rhs) noexcept
    : m_obj(rhs.m_obj)
{
    rhs.m_obj = nullptr;
}

Json& Json::operator=(Json&& rhs)
{
    json_decref(m_obj);
    m_obj = rhs.m_obj;
    rhs.m_obj = nullptr;
    m_errormsg.clear();
    return *this;
}

std::string Json::get_string(const string& key) const
{
    string rval;
    auto keyc = key.c_str();
    json_t* obj = json_object_get(m_obj, keyc);
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
                m_errormsg = mxb::string_printf(val_is_null, keyc);
            }
            else
            {
                m_errormsg = mxb::string_printf("%s is not a json string", keyc);
            }
        }
    }
    else
    {
        m_errormsg = mxb::string_printf(key_not_found, keyc);
    }
    return rval;
}

int64_t Json::get_int(const string& key) const
{
    int64_t rval = 0;
    auto keyc = key.c_str();
    json_t* obj = json_object_get(m_obj, keyc);
    if (obj)
    {
        if (json_is_integer(obj))
        {
            rval = json_integer_value(obj);
        }
        else if (json_is_null(obj))
        {
            m_errormsg = mxb::string_printf(val_is_null, keyc);
        }
        else
        {
            m_errormsg = mxb::string_printf("%s is not a json integer", keyc);
        }
    }
    else
    {
        m_errormsg = mxb::string_printf(key_not_found, keyc);
    }
    return rval;
}

Json Json::get_object(const string& key) const
{
    auto keyc = key.c_str();
    json_t* obj = json_object_get(m_obj, keyc);
    if (!obj)
    {
        m_errormsg = mxb::string_printf(key_not_found, keyc);
    }
    return Json(obj);
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

bool Json::try_get_string(const string& key, std::string* out) const
{
    bool rval = false;
    auto keyc = key.c_str();
    json_t* obj = json_object_get(m_obj, keyc);
    if (obj && json_is_string(obj))
    {
        *out = json_string_value(obj);
        rval = true;
    }
    return rval;
}
