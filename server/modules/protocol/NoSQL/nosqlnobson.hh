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

inline bool is_number(bsoncxx::type t)
{
    return is_integer(t) || is_double(t);
}

inline bool is_string(bsoncxx::type t)
{
    return t == bsoncxx::type::k_utf8;
}

inline bool is_null(bsoncxx::type t)
{
    return t == bsoncxx::type::k_null;
}

/**
 * bsoncxx::types::bson_value::view
 */

inline bool is_integer(bsoncxx::types::bson_value::view v)
{
    return is_integer(v.type());
}

inline bool is_double(bsoncxx::types::bson_value::view v)
{
    return is_double(v.type());
}

inline bool is_number(bsoncxx::types::bson_value::view v)
{
    auto t = v.type();
    return is_integer(t) || is_double(t);
}

inline bool is_string(bsoncxx::types::bson_value::view v)
{
    return is_string(v.type());
}

inline bool is_null(bsoncxx::types::bson_value::view v)
{
    return is_null(v.type());
}

bool is_zero(bsoncxx::types::bson_value::view v);

bool is_truthy(bsoncxx::types::bson_value::view v);

template<typename T>
bool get_integer(bsoncxx::types::bson_value::view view, T* pValue);

template<>
bool get_integer(bsoncxx::types::bson_value::view view, int64_t* pValue);

template<>
inline bool get_integer(bsoncxx::types::bson_value::view view, int32_t* pValue)
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
T get_integer(bsoncxx::types::bson_value::view view);

template<>
int64_t get_integer(bsoncxx::types::bson_value::view view);

template<>
inline int32_t get_integer(bsoncxx::types::bson_value::view view)
{
    return get_integer<int64_t>(view);
}

inline bool get_double(bsoncxx::types::bson_value::view view, double* pValue)
{
    bool rv = is_double(view);

    if (rv)
    {
        *pValue = view.get_double();
    }

    return rv;
}

double get_double(bsoncxx::types::bson_value::view view);

template<typename T>
bool get_number(bsoncxx::types::bson_value::view view, T* pValue);

template<>
bool get_number(bsoncxx::types::bson_value::view view, int64_t* pValue);

template<>
inline bool get_number(bsoncxx::types::bson_value::view view, int32_t* pValue)
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
T get_number(bsoncxx::types::bson_value::view view);

template<>
int64_t get_number(bsoncxx::types::bson_value::view view);

template<>
inline int32_t get_number(bsoncxx::types::bson_value::view view)
{
    return get_number<int64_t>(view);
}

template<>
double get_number(bsoncxx::types::bson_value::view view);

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

void to_json_expression(std::ostream& out, bsoncxx::types::bson_value::view view);
std::string to_json_expression(bsoncxx::types::bson_value::view view);

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

inline bool is_number(bsoncxx::array::element e)
{
    return is_number(e.type());
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

inline bool is_number(bsoncxx::document::element e)
{
    return is_number(e.type());
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

}
}
