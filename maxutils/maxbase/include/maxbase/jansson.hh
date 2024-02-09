/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxbase/ccdefs.hh>
#include <memory>
#include <string>
#include <jansson.h>
#include <maxbase/assert.hh>

namespace std
{

template<>
struct default_delete<json_t>
{
    void operator()(json_t* pJson)
    {
        json_decref(pJson);
    }
};
}

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
 * @brief Return value at provided JSON Pointer
 *
 * @see https://datatracker.ietf.org/doc/html/rfc6901
 *
 * @param json     JSON object
 * @param json_ptr JSON Pointer to object
 *
 * @return Pointed value or NULL if no value is found
 */
json_t* json_ptr(const json_t* json, const char* json_ptr);

/**
 * Get the type of the JSON as a string
 *
 * @param json The JSON object to inspect
 *
 * @return The human-readable JSON type
 */
const char* json_type_to_string(const json_t* json);

/**
 * Remove null values from JSON objects
 *
 * Removes any keys with JSON null values.
 *
 * @param json JSON to modify
 */
void json_remove_nulls(json_t* json);

/**
 * @brief Check if the value at the provided JSON Pointer is of a certain type
 *
 * @param json     JSON object
 * @param json_ptr JSON Pointer to object
 * @param type     JSON type that is expected
 *
 * @return False if the object was found but it was not of the expected type. True in all other cases.
 */
bool json_is_type(json_t* json, const char* json_ptr, json_type type);

}
