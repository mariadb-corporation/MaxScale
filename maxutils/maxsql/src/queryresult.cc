/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxsql/queryresult.hh>
#include <maxbase/assert.h>
#include <maxbase/format.hh>

using std::string;
using mxb::string_printf;

namespace maxsql
{
QueryResult::QueryResult(MYSQL_RES* resultset)
        : m_resultset(resultset)
{
    mxb_assert(m_resultset);
    auto columns = mysql_num_fields(m_resultset);
    MYSQL_FIELD* field_info = mysql_fetch_fields(m_resultset);
    for (int64_t column_index = 0; column_index < columns; column_index++)
    {
        string key(field_info[column_index].name);
        // TODO: Think of a way to handle duplicate names nicely. Currently this should only be used
        // for known queries.
        mxb_assert(m_col_indexes.count(key) == 0);
        m_col_indexes[key] = column_index;
    }
}

QueryResult::~QueryResult()
{
    mxb_assert(m_resultset);
    mysql_free_result(m_resultset);
}

bool QueryResult::next_row()
{
    m_rowdata = mysql_fetch_row(m_resultset);
    if (m_rowdata)
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

int64_t QueryResult::get_col_count() const
{
    return mysql_num_fields(m_resultset);
}

int64_t QueryResult::get_row_count() const
{
    return mysql_num_rows(m_resultset);
}

int64_t QueryResult::get_col_index(const string& col_name) const
{
    auto iter = m_col_indexes.find(col_name);
    return (iter != m_col_indexes.end()) ? iter->second : -1;
}

string QueryResult::get_string(int64_t column_ind) const
{
    mxb_assert(column_ind < get_col_count() && column_ind >= 0 && m_rowdata);
    char* data = m_rowdata[column_ind];
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
    return parse_integer(column_ind, "integer");
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

/**
 * Parse a 64bit integer. On parse error an error flag is set.
 *
 * @param column_ind Column index
 * @param target_type The final conversion target type.
 * @return Conversion result
 */
int64_t QueryResult::parse_integer(int64_t column_ind, const std::string& target_type) const
{
    mxb_assert(column_ind < get_col_count() && column_ind >= 0 && m_rowdata);
    int64_t rval = 0;
    char* data_elem = m_rowdata[column_ind];
    if (data_elem == nullptr)
    {
        set_error(column_ind, target_type);
    }
    else
    {
        errno = 0;
        char* endptr = nullptr;
        auto parsed = strtoll(data_elem, &endptr, 10);
        if (errno == 0 && *endptr == '\0')
        {
            rval = parsed;
        }
        else
        {
            set_error(column_ind, target_type);
        }
    }
    return rval;
}

bool QueryResult::get_bool(int64_t column_ind) const
{
    const string target_type = "boolean";
    bool rval = false;
    auto val = parse_integer(column_ind, target_type);
    if (!error())
    {
        if (val < 0 || val > 1)
        {
            set_error(column_ind, target_type);
        }
        else
        {
            rval = (val == 1);
        }
    }
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

bool QueryResult::field_is_null(int64_t column_ind) const
{
    mxb_assert(column_ind < get_col_count() && column_ind >= 0 && m_rowdata);
    return m_rowdata[column_ind] == nullptr;
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
    char* field_value = m_rowdata[column_ind];
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