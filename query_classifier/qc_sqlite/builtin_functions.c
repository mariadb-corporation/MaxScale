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

#include "builtin_functions.h"
#include <stdlib.h>
#include <string.h>
#include <maxscale/debug.h>

static struct
{
    bool inited;
} unit = { false };

// The functions have been taken from:
// https://mariadb.com/kb/en/mariadb/functions-and-operators/

static const char* BUILTIN_FUNCTIONS[] =
{
    /*
     * Bit Functions and Operators
     * https://mariadb.com/kb/en/mariadb/bit-functions-and-operators
     */
    "bit_count",

    /*
     * Control Flow Functions
     * https://mariadb.com/kb/en/mariadb/control-flow-functions/
     */
    "if",
    "ifnull",
    "nullif",

    /*
     * Date and Time Functions
     * https://mariadb.com/kb/en/mariadb/date-and-time-functions/
     */
    "adddate",
    "addtime",
    "convert_tz",
    "curdate",
    "current_date",
    "current_time",
    "current_timestamp",
    "curtime",
    "date",
    "datediff",
    "date_add",
    "date_format",
    "date_sub",
    "day",
    "dayname",
    "dayofmonth",
    "dayofweek",
    "dayofyear",
    "extract",
    "from_days",
    "from_unixtime",
    "get_format",
    "hour",
    "last_day",
    "localtime",
    "localtimestamp",
    "makedate",
    "maketime",
    "microsecond",
    "minute",
    "month",
    "monthname",
    "now",
    "period_add",
    "period_diff",
    "quarter",
    "second",
    "sec_to_time",
    "str_to_date",
    "subdate",
    "subtime",
    "sysdate",
    "time",
    "timediff"
    "timestamp",
    "timestampadd",
    "timestampdiff",
    "time_format",
    "time_to_sec",
    "to_days",
    "to_seconds",
    "unix_timestamp",
    "utc_date",
    "utc_time",
    "week",
    "weekday",
    "weekofyear",
    "year",
    "yearweek",

    /*
     * Columns Functions
     * https://mariadb.com/kb/en/mariadb/dynamic-columns-functions/
     */
    "column_check",
    "column_exists",
    "column_get",
    "column_json",
    "column_list",

    /*
     * Encryption, Hashing and Compression Functions
     * https://mariadb.com/kb/en/mariadb/encryption-hashing-and-compression-functions/
     */
    "aes_decrypt",
    "aes_encrypt",
    "compress",
    "decode",
    "des_decrypt",
    "des_encrypt",
    "encode",
    "encrypt",
    "md5",
    "old_password",
    "password",
    "sha1",
    "sha2",
    "uncompress",
    "uncompressed_length",

    /*
     * Comparison Operators
     * https://mariadb.com/kb/en/mariadb/comparison-operators/
     */
    "coalesce",
    "greatest",
    "interval",
    "isnull",
    "least",

    /*
     * Functions and Modifiers for use with GROUP BY
     * https://mariadb.com/kb/en/mariadb/functions-and-modifiers-for-use-with-group-by/
     */
    "avg",
    "bit_and",
    "bit_or",
    "bit_xor",
    "count",
    "group_concat",
    "max",
    "min",
    "std",
    "stddev",
    "stddev_pop",
    "stddev_samp",
    "sum",
    "variance",
    "var_pop",
    "var_samp",

    /*
     * Geographic Functions
     * https://mariadb.com/kb/en/mariadb/geographic-functions/
     */

    // Geometry Constructors
    // https://mariadb.com/kb/en/mariadb/geometry-constructors/
    "geometrycollection",
    "linestring",
    "multilinestring",
    "multipoint",
    "point",
    "polygon",
    "st_buffer",
    "st_convexhull",
    "st_intersection",
    "st_pointonsurface",
    "st_symdifference",
    "std_union",

    // Geometry Properties
    // https://mariadb.com/kb/en/mariadb/geometry-properties/
    // TODO

    // Geometry Relations
    // TODO

    // LineString Properties
    // TODO

    // MBR
    // TODO

    // Point Propertoes
    // TODO

    // Polygon Properties
    // TODO

    // WKB
    // TODO

    // WKT
    // https://mariadb.com/kb/en/mariadb/wkt/
    "MLineFromText",
    "MPointFromText",
    "MPolyFromText",
    "ST_AsText",
    "ST_ASWKT",
    "ST_GeomCollFromText",
    "ST_GeometryFromText",
    "ST_LineFromText",
    "ST_PointFromText",
    "ST_PolyFromText",
    // Deprecated
    "geomfromtext",

    /*
     * Information Functions
     * https://mariadb.com/kb/en/mariadb/information-functions/
     */
    "benchmark",
    "binlog_gtid_pos",
    "charset",
    "coercibility",
    "collation",
    "connection_id",
    "current_role",
    "current_user",
    "database",
    "decode_histogram",
    "found_rows",
    "last_insert_id",
    "row_count",
    "schema",
    "session_user",
    "system_user",
    "user",
    "version",

    /*
     * Miscellanesous Functions
     * https://mariadb.com/kb/en/mariadb/miscellaneous-functions/
     */
    "default",
    "get_lock",
    "inet6_aton",
    "inet6_ntoa",
    "inet_aton",
    "inet_ntoa",
    "is_free_lock",
    "is_ipv4",
    "is_ipv4_compat",
    "is_ipv4_mapped",
    "is_ipv6",
    "is_used_lock",
    "last_value",
    "master_gtid_wait",
    "master_pos_wait",
    "name_const",
    "release_lock",
    "sleep",
    "uuid",
    "uuid_short",
    "values",

    /*
     * Numeric Functions
     * https://mariadb.com/kb/en/mariadb/numeric-functions/
     */
    "abs",
    "acos",
    "asin",
    "atan",
    "atan2",
    "ceil",
    "ceiling",
    "conv",
    "cos",
    "cot",
    "crc32",
    "degrees",
    "div",
    "exp",
    "floor",
    "greatest",
    "least",
    "ln",
    "log",
    "log10",
    "log2",
    "mod",
    "oct",
    "pi",
    "pow",
    "power",
    "radians",
    "rand",
    "round",
    "sign",
    "sin",
    "sqrt",
    "tan",
    "truncate",

    /*
     * String Functions
     * https://mariadb.com/kb/en/mariadb/string-functions/
     */
    "ascii",
    "bin",
    "bit_length",
    "cast",
    "char",
    "character_length",
    "char_length",
    "concat",
    "concat_ws",
    "convert",
    "elt",
    "export_set",
    "extractvalue",
    "field",
    "find_in_set",
    "format",
    "hex",
    "insert",
    "instr",
    "lcase",
    "left",
    "length",
    "like",
    "load_file",
    "locate",
    "lower",
    "lpad",
    "ltrim",
    "make_set",
    "mid",
    "octet_length",
    "ord",
    "position",
    "quote",
    "repeat",
    "replace",
    "reverse",
    "right",
    "rpad",
    "rtrim",
    "soundex",
    "space",
    "strcmp",
    "substr",
    "substring",
    "substring_index",
    "trim",
    "ucase",
    "unhex",
    "updatexml",
    "upper",
    "from_base64",
    "to_base64",
    "weight_string",

    /*
     * Regex functions
     * https://mariadb.com/kb/en/mariadb/regular-expressions-functions/
     */
    "regexp",
    "regexp_instr",
    "regexp_replace",
    "regexp_substr",
    "rlike",

    /*
     * http://dev.mysql.com/doc/refman/5.7/en/row-subqueries.html
     */
    "row"
};

const size_t N_BUILTIN_FUNCTIONS = sizeof(BUILTIN_FUNCTIONS) / sizeof(BUILTIN_FUNCTIONS[0]);

static const char* BUILTIN_10_2_3_FUNCTIONS[] =
{
    //
    // JSON functions: https://mariadb.com/kb/en/mariadb/json-functions
    //
    "json_array",
    "json_array_append",
    "json_array_insert",
    "json_compact",
    "json_contains",
    "json_contains_path",
    "json_depth",
    "json_detailed",
    "json_exists",
    "json_extract",
    "json_insert",
    "json_keys",
    "json_length",
    "json_loose",
    "json_merge",
    "json_object",
    "json_query",
    "json_quote",
    "json_remove"
    "json_replace",
    "json_search",
    "json_set",
    "json_type",
    "json_unquote",
    "json_valid",
    "json_value",

    //
    // Window functions: https://mariadb.com/kb/en/mariadb/window-functions/
    //
    "cume_dist",
    "dense_rank",
    "ntile",
    "percent_rank",
    "rank",
    "row_number",
};

const size_t N_BUILTIN_10_2_3_FUNCTIONS =
    sizeof(BUILTIN_10_2_3_FUNCTIONS) / sizeof(BUILTIN_10_2_3_FUNCTIONS[0]);

static const char* ORACLE_FUNCTIONS[] =
{
    "nvl",
    "nvl2"
};

const size_t N_ORACLE_FUNCTIONS = sizeof(ORACLE_FUNCTIONS) / sizeof(ORACLE_FUNCTIONS[0]);

// NOTE: sort_compare and search_compare are not identical, so don't
// NOTE: optimize either of them away.
static int sort_compare(const void* key, const void* value)
{
    return strcasecmp(*(const char**) key, *(const char**) value);
}

static int search_compare(const void* key, const void* value)
{
    return strcasecmp((const char*) key, *(const char**) value);
}

//
// API
//

void init_builtin_functions()
{
    ss_dassert(!unit.inited);

    qsort(BUILTIN_FUNCTIONS, N_BUILTIN_FUNCTIONS, sizeof(char*), sort_compare);
    qsort(BUILTIN_10_2_3_FUNCTIONS, N_BUILTIN_10_2_3_FUNCTIONS, sizeof(char*), sort_compare);
    qsort(ORACLE_FUNCTIONS, N_ORACLE_FUNCTIONS, sizeof(char*), sort_compare);

    unit.inited = true;
}

void finish_builtin_functions()
{
    ss_dassert(unit.inited);
    unit.inited = false;
}

bool is_builtin_readonly_function(const char* key,
                                  uint32_t major, uint32_t minor, uint32_t patch,
                                  bool check_oracle)
{
    ss_dassert(unit.inited);

    char* value = bsearch(key, BUILTIN_FUNCTIONS, N_BUILTIN_FUNCTIONS, sizeof(char*), search_compare);

    if (!value)
    {
        if ((major > 10) ||
            ((major == 10) && (minor > 2)) ||
            ((major == 10) && (minor == 2) && (patch >= 3)))
        {
            value = bsearch(key, BUILTIN_10_2_3_FUNCTIONS, N_BUILTIN_10_2_3_FUNCTIONS,
                            sizeof(char*), search_compare);
        }
    }

    if (!value && check_oracle)
    {
        value = bsearch(key, ORACLE_FUNCTIONS, N_ORACLE_FUNCTIONS, sizeof(char*), search_compare);
    }

    return value ? true : false;
}
