/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxsql/queryresult.hh>

#include <mysql.h>
#include <maxbase/assert.h>
#include <maxbase/format.hh>
#include <memory>

using std::string;
using mxb::string_printf;

namespace
{
const string type_integer = "integer";
const string type_uinteger = "unsigned integer";
const string type_boolean = "boolean";
}
namespace maxsql
{

bool QueryResult::next_row()
{
    if (advance_row())
    {
        m_current_row_ind++;
        m_error = ConversionError();    // Reset error
        return true;
    }
    else
    {
        m_current_row_ind = -1;
        return false;
    }
}

int64_t QueryResult::get_current_row_index() const
{
    return m_current_row_ind;
}

int64_t QueryResult::get_col_index(const string& col_name) const
{
    auto iter = m_col_indexes.find(col_name);
    return (iter != m_col_indexes.end()) ? iter->second : -1;
}

string QueryResult::get_string(int64_t column_ind) const
{
    mxb_assert(column_ind < get_col_count() && column_ind >= 0);
    auto data = row_elem(column_ind);
    return data ? data : "";
}

string QueryResult::get_string(const std::string& name) const
{
    auto idx = get_col_index(name);
    if (idx != -1)
    {
        return get_string(idx);
    }
    return "";
}

int64_t QueryResult::get_int(int64_t column_ind) const
{
    int64_t rval = 0;
    auto int_parser = [&rval](const char* data_elem) {
            bool success = false;
            errno = 0;
            char* endptr = nullptr;
            auto parsed = strtoll(data_elem, &endptr, 10);
            if (errno == 0 && *endptr == '\0')
            {
                rval = parsed;
                success = true;
            }
            return success;
        };

    call_parser(int_parser, column_ind, type_integer);
    return rval;
}

int64_t QueryResult::get_int(const std::string& name) const
{
    auto idx = get_col_index(name);
    if (idx != -1)
    {
        return get_int(idx);
    }
    return 0;
}

uint64_t QueryResult::get_uint(int64_t column_ind) const
{
    uint64_t rval = 0;
    auto uint_parser = [&rval](const char* data_elem) {
            bool success = false;
            errno = 0;
            char* endptr = nullptr;
            auto parsed = strtoull(data_elem, &endptr, 10);
            if (errno == 0 && *endptr == '\0')
            {
                rval = parsed;
                success = true;
            }
            return success;
        };

    call_parser(uint_parser, column_ind, type_uinteger);
    return rval;
}

bool QueryResult::get_bool(int64_t column_ind) const
{
    bool rval = false;
    auto bool_parser = [&rval](const char* data_elem) {
            bool success = false;
            char c = *data_elem;
            if (c == '1' || c == 'Y' || c == 'y')
            {
                rval = true;
                success = true;
            }
            else if (c == '0' || c == 'N' || c == 'n')
            {
                success = true;
            }
            return success;
        };

    call_parser(bool_parser, column_ind, type_boolean);
    return rval;
}

bool QueryResult::get_bool(const std::string& name) const
{
    auto idx = get_col_index(name);
    if (idx != -1)
    {
        return get_bool(idx);
    }
    return 0;
}

void QueryResult::call_parser(const std::function<bool(const char*)>& parser, int64_t column_ind,
                              const std::string& target_type) const
{
    mxb_assert(column_ind < get_col_count() && column_ind >= 0);
    auto data_elem = row_elem(column_ind);
    if (data_elem == nullptr || !parser(data_elem))
    {
        set_error(column_ind, target_type);
    }
}

bool QueryResult::field_is_null(int64_t column_ind) const
{
    mxb_assert(column_ind < get_col_count() && column_ind >= 0);
    return row_elem(column_ind) == nullptr;
}

void QueryResult::set_error(int64_t column_ind, const string& target_type) const
{
    string col_name;
    // Find the column name.
    for (const auto& elem : m_col_indexes)
    {
        if (elem.second == column_ind)
        {
            col_name = elem.first;
            break;
        }
    }

    mxb_assert(!col_name.empty());
    // If the field value is null, then that is the cause of the error.
    auto field_value = row_elem(column_ind);
    if (field_value == nullptr)
    {
        m_error.set_null_value_error(target_type);
    }
    else
    {
        m_error.set_value_error(field_value, target_type);
    }
}

bool QueryResult::error() const
{
    return m_error.error();
}

string QueryResult::error_string() const
{
    return m_error.to_string();
}

QueryResult::QueryResult(std::vector<std::string>&& col_names)
{
    for (size_t column_index = 0; column_index < col_names.size(); column_index++)
    {
        const auto& key = col_names[column_index];
        // TODO: Think of a way to handle duplicate names nicely. Currently this should only be used
        // for known queries.
        mxb_assert(m_col_indexes.count(key) == 0);
        m_col_indexes[key] = column_index;
    }
}

void QueryResult::ConversionError::set_value_error(const string& field_value, const string& target_type)
{
    mxb_assert(!target_type.empty());
    // The error container only has space for one error.
    if (m_target_type.empty())
    {
        m_field_value = field_value;
        m_target_type = target_type;
    }
}

void QueryResult::ConversionError::set_null_value_error(const string& target_type)
{
    mxb_assert(!target_type.empty());
    if (m_target_type.empty())
    {
        m_field_was_null = true;
        m_target_type = target_type;
    }
}

string QueryResult::ConversionError::to_string() const
{
    string rval;
    if (!m_target_type.empty())
    {
        rval = "Cannot convert ";
        if (m_field_was_null)
        {
            rval += mxb::string_printf("a null field to %s.", m_target_type.c_str());
        }
        else
        {
            rval += mxb::string_printf("field '%s' to %s.", m_field_value.c_str(), m_target_type.c_str());
        }
    }
    return rval;
}

bool QueryResult::ConversionError::error() const
{
    return !m_target_type.empty();
}
}
