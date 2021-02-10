/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/jansson.hh>

/**
 * @brief Return value at provided JSON Pointer
 *
 * @param json     JSON object
 * @param json_ptr JSON Pointer to object
 *
 * @return Pointed value or NULL if no value is found
 */
json_t* mxs_json_pointer(json_t* json, const char* json_ptr);

/**
 * @brief Check if the value at the provided JSON Pointer is of a certain type
 *
 * @param json     JSON object
 * @param json_ptr JSON Pointer to object
 * @param type     JSON type that is expected
 *
 * @return False if the object was found but it was not of the expected type. True in all other cases.
 */
bool mxs_json_is_type(json_t* json, const char* json_ptr, json_type type);

namespace maxscale
{

/**
 * Unpack a JSON string
 *
 * @param json JSON to unpack
 * @param ptr  JSON pointer into `json` to unpack
 * @param t    Pointer to object to assign to
 *
 * @return True if value was unpacked
 */
bool get_json_string(json_t* json, const char* ptr, std::string* out);

/**
 * Unpack a JSON integer
 *
 * @param json JSON to unpack
 * @param ptr  JSON pointer into `json` to unpack
 * @param t    Pointer to object to assign to
 *
 * @return True if value was unpacked
 */
bool get_json_int(json_t* json, const char* ptr, int64_t* out);

/**
 * Unpack a JSON float
 *
 * @param json JSON to unpack
 * @param ptr  JSON pointer into `json` to unpack
 * @param t    Pointer to object to assign to
 *
 * @return True if value was unpacked
 */
bool get_json_float(json_t* json, const char* ptr, double* out);

/**
 * Unpack a JSON boolean
 *
 * @param json JSON to unpack
 * @param ptr  JSON pointer into `json` to unpack
 * @param t    Pointer to object to assign to
 *
 * @return True if value was unpacked
 */
bool get_json_bool(json_t* json, const char* ptr, bool* out);

/**
 * Remove null values from JSON objects
 *
 * Removes any keys with JSON null values.
 *
 * @param json JSON to modify
 */
void json_remove_nulls(json_t* json);

/**
 * Combine `dest` and `src` into one object
 *
 * Removes JSON nulls and updates `dest` with the contents of `src` (both objects are modified).
 *
 * @param dest JSON object where the result is stored
 * @param src  JSON object from which the values are taken
 *
 * @return The value of `dest`. Note that reference counts aren't incremented.
 */
json_t* json_merge(json_t* dest, json_t* src);
}
