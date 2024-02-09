/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#include <maxscale/ccdefs.hh>
#include <maxbase/json.hh>


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

}
