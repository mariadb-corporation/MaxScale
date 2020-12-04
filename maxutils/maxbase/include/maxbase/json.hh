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

#pragma once

#include <string>
#include <vector>

struct json_t;

namespace maxbase
{

/**
 * Wrapper class for Jansson json-objects.
 */
class Json
{
public:

    enum class Type
    {
        OBJECT,     /**< Json object */
        ARRAY,      /**< Json array */
        JS_NULL     /**< Json null */
    };

    explicit Json(Type type = Type::OBJECT);
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

    bool contains(const std::string& key) const;
    bool is_null(const std::string& key) const;

    Json get_object(const char* key) const;
    Json get_object(const std::string& key) const;

    std::string get_string(const char* key) const;
    std::string get_string(const std::string& key) const;

    int64_t get_int(const char* key) const;
    int64_t get_int(const std::string& key) const;

    bool try_get_int(const std::string& key, int64_t* out) const;

    std::vector<Json> get_array_elems(const std::string& key) const;

    const std::string& error_msg() const;

    bool valid() const;

    void set_object(const char* key, Json&& value);
    void set_string(const char* key, const char* value);
    void set_int(const char* key, int64_t value);
    void add_array_elem(Json&& elem);
    bool save(const std::string& filepath);
    bool load(const std::string& filepath);

    bool ok() const;
private:
    json_t*             m_obj{nullptr}; /**< Managed json-object */
    mutable std::string m_errormsg;     /**< Error message container */

    void reset();
    void swap(Json& rhs) noexcept;
};
}
