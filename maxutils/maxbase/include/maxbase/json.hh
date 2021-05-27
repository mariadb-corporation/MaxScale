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
 * @brief Convenience function for dumping JSON into a string
 *
 * @param json  JSON to dump
 * @param flags Optional flags passed to the jansson json_dump function
 *
 * @return The JSON in string format
 */
std::string json_dump(const json_t* json, int flags = 0);

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
    bool try_get_string(const std::string& key, std::string* out) const;

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

    /**
     * Get underlying JSON pointer
     *
     * @return Pointer to the managed JSON object
     */
    json_t* get_json() const;

    /**
     * Return contents as a string
     *
     * @return The JSON as a string
     */
    std::string to_string() const;

    /**
     * Replace the current JSON object
     *
     * @param obj Object to use. The reference is stolen.
     */
    void reset(json_t* obj = nullptr);

private:
    json_t*             m_obj{nullptr}; /**< Managed json-object */
    mutable std::string m_errormsg;     /**< Error message container */

    void swap(Json& rhs) noexcept;
};
}
