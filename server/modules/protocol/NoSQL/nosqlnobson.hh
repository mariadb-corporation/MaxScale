/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include <sstream>
#include <bsoncxx/array/element.hpp>
#include <bsoncxx/document/element.hpp>
#include <bsoncxx/types/value.hpp>

namespace nosql
{

// "nobson" to prevent confusion with bsoncxx.
namespace nobson
{

/**
 * bsoncxx::type
 */
inline bool is_integer(bsoncxx::type t)
{
    return t == bsoncxx::type::k_int32 || t == bsoncxx::type::k_int64;
}

inline bool is_double(bsoncxx::type t)
{
    return t == bsoncxx::type::k_double;
}

inline bool is_decimal128(bsoncxx::type t)
{
    return t == bsoncxx::type::k_decimal128;
}

enum class NumberApproach
{
    IGNORE_DECIMAL128,
    REJECT_DECIMAL128
};

bool check_if_decimal128(bsoncxx::type t, NumberApproach approach);

inline bool is_number(bsoncxx::type t, NumberApproach approach)
{
    return (is_integer(t) || is_double(t) || check_if_decimal128(t, approach));
}

inline bool is_string(bsoncxx::type t)
{
    return t == bsoncxx::type::k_utf8;
}

inline bool is_null(bsoncxx::type t)
{
    return t == bsoncxx::type::k_null;
}

inline bool is_undefined(bsoncxx::type t)
{
    return t == bsoncxx::type::k_undefined;
}

/**
 * bsoncxx::types::bson_value::view
 */

/**
 * Mathematical operations
 *
 * @param v  A number.
 *
 * @return The result of the mathematical operation. If @c is not a number, null is returned.
 */
bsoncxx::types::bson_value::value abs(const bsoncxx::types::bson_value::view& v);
bsoncxx::types::bson_value::value ceil(const bsoncxx::types::bson_value::view& v);
bsoncxx::types::bson_value::value exp(const bsoncxx::types::bson_value::view& v);
bsoncxx::types::bson_value::value floor(const bsoncxx::types::bson_value::view& v);
bsoncxx::types::bson_value::value log(const bsoncxx::types::bson_value::view& v);
bsoncxx::types::bson_value::value sqrt(const bsoncxx::types::bson_value::view& v);

inline bool is_integer(const bsoncxx::types::bson_value::view& v)
{
    return is_integer(v.type());
}

inline bool is_double(const bsoncxx::types::bson_value::view& v)
{
    return is_double(v.type());
}

inline bool is_number(const bsoncxx::types::bson_value::view& v, NumberApproach approach)
{
    return is_number(v.type(), approach);
}

inline bool is_string(const bsoncxx::types::bson_value::view& v)
{
    return is_string(v.type());
}

inline bool is_null(const bsoncxx::types::bson_value::view& v)
{
    return is_null(v.type());
}

inline bool is_undefined(const bsoncxx::types::bson_value::view& v)
{
    return is_undefined(v.type());
}

bool is_zero(const bsoncxx::types::bson_value::view& v);

bool is_truthy(const bsoncxx::types::bson_value::view& v);

template<typename T>
bool get_integer(const bsoncxx::types::bson_value::view& view, T* pValue);

template<>
bool get_integer(const bsoncxx::types::bson_value::view& view, int64_t* pValue);

template<>
inline bool get_integer(const bsoncxx::types::bson_value::view& view, int32_t* pValue)
{
    int64_t value;
    bool rv = get_integer<int64_t>(view, &value);

    if (rv)
    {
        *pValue = value;
    }

    return rv;
}

template<typename T>
T get_integer(const bsoncxx::types::bson_value::view& view);

template<>
int64_t get_integer(const bsoncxx::types::bson_value::view& view);

template<>
inline int32_t get_integer(const bsoncxx::types::bson_value::view& view)
{
    return get_integer<int64_t>(view);
}

inline bool get_double(const bsoncxx::types::bson_value::view& view, double* pValue)
{
    bool rv = is_double(view);

    if (rv)
    {
        *pValue = view.get_double();
    }

    return rv;
}

double get_double(const bsoncxx::types::bson_value::view& view);

template<typename T>
bool get_number(const bsoncxx::types::bson_value::view& view, T* pValue);

template<>
bool get_number(const bsoncxx::types::bson_value::view& view, int64_t* pValue);

template<>
inline bool get_number(const bsoncxx::types::bson_value::view& view, int32_t* pValue)
{
    int64_t value;
    bool rv = get_number<int64_t>(view, &value);

    if (rv)
    {
        *pValue = value;
    }

    return rv;
}

template<typename T>
T get_number(const bsoncxx::types::bson_value::view& view);

template<>
int64_t get_number(const bsoncxx::types::bson_value::view& view);

template<>
inline int32_t get_number(const bsoncxx::types::bson_value::view& view)
{
    return get_number<int64_t>(view);
}

template<>
double get_number(const bsoncxx::types::bson_value::view& view);

/**
 * Convert s BSON value to a JSON expression that can be used in a
 * SELECT statement. When the returned value (as a string in a
 * resultset) is is loaded, the result will be the original BSON value.
 *
 * For this to work, the outermost value must be a document or an array.
 *
 * @param out  A stream to write to.
 * @param x    Some BSON value
 */
void to_json_expression(std::ostream& out, bsoncxx::types::b_array x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_binary x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_bool x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_code x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_codewscope x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_date x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_dbpointer x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_decimal128 x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_document x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_double x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_int32 x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_int64 x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_maxkey x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_minkey x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_null x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_oid x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_regex x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_string x); // == b_utf8
void to_json_expression(std::ostream& out, bsoncxx::types::b_symbol x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_timestamp x);
void to_json_expression(std::ostream& out, bsoncxx::types::b_undefined x);

void to_json_expression(std::ostream& out, bsoncxx::oid oid);
void to_json_expression(std::ostream& out, bsoncxx::document::view doc);

void to_json_expression(std::ostream& out, const bsoncxx::types::bson_value::view& view);

std::string to_json_expression(const bsoncxx::types::bson_value::view& view);

/**
 * Convert a BSON value to the JSON code that was used when it was created.
 * Not a 100% match, so intended only to be used in error messages.
 *
 * @param out  A stream to write to.
 * @param x    Some BSON value
 *
 */
void to_bson_expression(std::ostream& out, bsoncxx::types::b_array x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_binary x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_bool x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_code x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_codewscope x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_date x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_dbpointer x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_decimal128 x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_document x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_double x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_int32 x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_int64 x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_maxkey x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_minkey x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_null x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_oid x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_regex x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_string x); // == b_utf8
void to_bson_expression(std::ostream& out, bsoncxx::types::b_symbol x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_timestamp x);
void to_bson_expression(std::ostream& out, bsoncxx::types::b_undefined x);

void to_bson_expression(std::ostream& out, bsoncxx::oid oid);
void to_bson_expression(std::ostream& out, bsoncxx::document::view doc);

void to_bson_expression(std::ostream& out, const bsoncxx::types::bson_value::view& view);

std::string to_bson_expression(const bsoncxx::types::bson_value::view& view);

/**
 * @param lhs  One value.
 * @param rhs  Another value.
 *
 * @return -1 if @c lhs is less than @c rhs
 *          0 if @c lhs is equal to @c rhs
 *          1 if @c lhs is greated than @c rhs
 */
int compare(const bsoncxx::types::bson_value::view& lhs, const bsoncxx::types::bson_value::view& rhs);

/**
 * bsoncxx::array::element
 */
inline bool is_integer(bsoncxx::array::element e)
{
    return is_integer(e.type());
}

inline bool is_double(bsoncxx::array::element e)
{
    return is_double(e.type());
}

inline bool is_number(bsoncxx::array::element e, NumberApproach approach)
{
    return is_number(e.type(), approach);
}

inline bool is_string(bsoncxx::array::element e)
{
    return is_string(e.type());
}

template<typename T>
inline bool get_integer(bsoncxx::array::element e, T* pValue)
{
    return get_integer(e.get_value(), pValue);
}

template<typename T>
inline T get_integer(bsoncxx::array::element e)
{
    return get_integer<T>(e.get_value());
}

inline bool get_double(bsoncxx::array::element e, double* pValue)
{
    return get_double(e.get_value(), pValue);
}

inline double get_double(bsoncxx::array::element e)
{
    return get_double(e.get_value());
}

template<typename T>
inline bool get_number(bsoncxx::array::element e, T* pValue)
{
    return get_number(e.get_value(), pValue);
}

template<typename T>
inline T get_number(bsoncxx::array::element e)
{
    return get_number<T>(e.get_value());
}

inline bool is_truthy(bsoncxx::array::element e)
{
    return is_truthy(e.get_value());
}

/**
 * bsonxx::document::element
 */
inline bool is_integer(bsoncxx::document::element e)
{
    return is_integer(e.type());
}

inline bool is_double(bsoncxx::document::element e)
{
    return is_double(e.type());
}

inline bool is_number(bsoncxx::document::element e, NumberApproach approach)
{
    return is_number(e.type(), approach);
}

inline bool is_string(bsoncxx::document::element e)
{
    return is_string(e.type());
}

template<typename T>
inline bool get_integer(bsoncxx::document::element e, T* pValue)
{
    return get_integer(e.get_value(), pValue);
}

template<typename T>
inline T get_integer(bsoncxx::document::element e)
{
    return get_integer<T>(e.get_value());
}

inline bool get_double(bsoncxx::document::element e, double* pValue)
{
    return get_double(e.get_value(), pValue);
}

inline double get_double(bsoncxx::document::element e)
{
    return get_double(e.get_value());
}

template<typename T>
inline bool get_number(bsoncxx::document::element e, T* pValue)
{
    return get_number(e.get_value(), pValue);
}

template<typename T>
inline T get_number(bsoncxx::document::element e)
{
    return get_number<T>(e.get_value());
}

inline bool is_truthy(bsoncxx::document::element e)
{
    return is_truthy(e.get_value());
}

/**
 * Conversions
 */
enum class ConversionResult
{
    OK,
    UNDERFLOW,
    OVERFLOW
};

ConversionResult convert(bsoncxx::decimal128 decimal128, double* pValue);
ConversionResult convert(bsoncxx::decimal128 decimal128, int32_t* pValue);
ConversionResult convert(bsoncxx::decimal128 decimal128, int64_t* pValue);

template<class N>
inline ConversionResult convert(bsoncxx::types::b_decimal128 decimal128, N* pValue)
{
    return convert(decimal128.value, pValue);
}

/**
 * Arithmetic Operations
 *
 * @param @c lhs  The left hand side.
 * @param @c rhs  The right hand side.
 *
 * @c is_number((lhs|rhs), NumberApproach::REJECT_DECIMAL128) must return
 * true for both arguments.
 *
 * @return The result of the operation.
 */

bsoncxx::types::bson_value::value add(const bsoncxx::types::bson_value::view& lhs,
                                      const bsoncxx::types::bson_value::view& rhs);

bsoncxx::types::bson_value::value sub(const bsoncxx::types::bson_value::view& lhs,
                                      const bsoncxx::types::bson_value::view& rhs);

bsoncxx::types::bson_value::value mul(const bsoncxx::types::bson_value::view& lhs,
                                      const bsoncxx::types::bson_value::view& rhs);

bsoncxx::types::bson_value::value div(const bsoncxx::types::bson_value::view& lhs,
                                      const bsoncxx::types::bson_value::view& rhs);

bsoncxx::types::bson_value::value mod(const bsoncxx::types::bson_value::view& lhs,
                                      const bsoncxx::types::bson_value::view& rhs);

bsoncxx::types::bson_value::value pow(const bsoncxx::types::bson_value::view& lhs,
                                      const bsoncxx::types::bson_value::view& rhs);

}

}

inline bool operator < (const bsoncxx::types::bson_value::view& lhs,
                        const bsoncxx::types::bson_value::view& rhs)
{
    return nosql::nobson::compare(lhs, rhs) < 0;
}

inline bool operator <= (const bsoncxx::types::bson_value::view& lhs,
                        const bsoncxx::types::bson_value::view& rhs)
{
    return nosql::nobson::compare(lhs, rhs) <= 0;
}

inline bool operator > (const bsoncxx::types::bson_value::view& lhs,
                        const bsoncxx::types::bson_value::view& rhs)
{
    return nosql::nobson::compare(lhs, rhs) > 0;
}

inline bool operator >= (const bsoncxx::types::bson_value::view& lhs,
                        const bsoncxx::types::bson_value::view& rhs)
{
    return nosql::nobson::compare(lhs, rhs) >= 0;
}
