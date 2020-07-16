#pragma once

#include <string>
#include <vector>

struct json_t;

/**
 * Wrapper class for Jansson json-objects.
 */
class Json
{
public:

    Json() = default;
    ~Json();

    /**
     * Construct a new Json wrapper object. Increments reference count of obj.
     *
     * @param obj The object to manage
     */
    explicit Json(json_t* obj);

    Json(const Json& rhs);
    Json& operator=(const Json& rhs);

    Json(Json&& rhs) noexcept;
    Json& operator=(Json&& rhs);

    /**
     * Load data from json string. Removes any currently held object.
     *
     * @param source Source string
     * @return True on success
     */
    bool load_string(const std::string& source);

    Json        get_object(const std::string& key) const;
    std::string get_string(const std::string& key) const;
    int64_t     get_int(const std::string& key) const;

    std::vector<Json> get_array_elems(const std::string& key) const;

    std::string error_msg() const;
    bool valid() const;

private:
    json_t*             m_obj {nullptr};/**< Managed json-object */
    mutable std::string m_errormsg;     /**< Error message container */
};
