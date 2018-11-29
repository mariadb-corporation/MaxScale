/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxsql/mariadb.hh>
#include <time.h>
#include <errmsg.h>
#include <string>
#include <string.h>
#include <maxbase/alloc.h>
#include <maxbase/assert.h>
#include <maxbase/format.hh>

using std::string;

namespace
{
struct THIS_UNIT
{
    bool log_statements;    // Should all statements sent to server be logged?
};

static THIS_UNIT this_unit =
{
    false
};
}

namespace maxsql
{

int mysql_query_ex(MYSQL* conn, const std::string& query, int query_retries, time_t query_retry_timeout)
{
    const char* query_cstr = query.c_str();
    time_t start = time(NULL);
    int rc = mysql_query(conn, query_cstr);

    for (int n = 0; rc != 0 && n < query_retries && mysql_is_net_error(mysql_errno(conn))
         && time(NULL) - start < query_retry_timeout; n++)
    {
        rc = mysql_query(conn, query_cstr);
    }

    if (this_unit.log_statements)
    {
        const char* host = "0.0.0.0";
        unsigned int port = 0;
        MXB_AT_DEBUG(int rc1 = ) mariadb_get_info(conn, MARIADB_CONNECTION_HOST, &host);
        MXB_AT_DEBUG(int rc2 = ) mariadb_get_info(conn, MARIADB_CONNECTION_PORT, &port);
        mxb_assert(!rc1 && !rc2);
        MXB_NOTICE("SQL([%s]:%u): %d, \"%s\"", host, port, rc, query_cstr);
    }

    return rc;
}

bool mysql_is_net_error(unsigned int errcode)
{
    switch (errcode)
    {
    case CR_SOCKET_CREATE_ERROR:
    case CR_CONNECTION_ERROR:
    case CR_CONN_HOST_ERROR:
    case CR_IPSOCK_ERROR:
    case CR_SERVER_GONE_ERROR:
    case CR_TCP_CONNECTION:
    case CR_SERVER_LOST:
        return true;

    default:
        return false;
    }
}

void mysql_set_log_statements(bool enable)
{
    this_unit.log_statements = enable;
}

bool mysql_get_log_statements()
{
    return this_unit.log_statements;
}

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


/**
 * @brief Calculate the length of a length-encoded integer in bytes
 *
 * @param ptr Start of the length encoded value
 * @return Number of bytes before the actual value
 */
size_t leint_bytes(const uint8_t* ptr)
{
    uint8_t val = *ptr;
    if (val < 0xfb)
    {
        return 1;
    }
    else if (val == 0xfc)
    {
        return 3;
    }
    else if (val == 0xfd)
    {
        return 4;
    }
    else
    {
        return 9;
    }
}

/**
 * @brief Converts a length-encoded integer to @c uint64_t
 *
 * @see https://dev.mysql.com/doc/internals/en/integer.html
 * @param c Pointer to the first byte of a length-encoded integer
 * @return The value converted to a standard unsigned integer
 */
uint64_t leint_value(const uint8_t* c)
{
    uint64_t sz = 0;
    if (*c < 0xfb)
    {
        sz = *c;
    }
    else if (*c == 0xfc)
    {
        memcpy(&sz, c + 1, 2);
    }
    else if (*c == 0xfd)
    {
        memcpy(&sz, c + 1, 3);
    }
    else if (*c == 0xfe)
    {
        memcpy(&sz, c + 1, 8);
    }
    else
    {
        mxb_assert(*c == 0xff);
        MXB_ERROR("Unexpected length encoding '%x' encountered when reading length-encoded integer.", *c);
    }
    return sz;
}

/**
 * Converts a length-encoded integer into a standard unsigned integer
 * and advances the pointer to the next unrelated byte.
 *
 * @param c Pointer to the first byte of a length-encoded integer
 */
uint64_t leint_consume(uint8_t** c)
{
    uint64_t rval = leint_value(*c);
    *c += leint_bytes(*c);
    return rval;
}

/**
 * @brief Consume and duplicate a length-encoded string
 *
 * Converts a length-encoded string to a C string and advances the pointer to
 * the first byte after the string. The caller is responsible for freeing
 * the returned string.
 * @param c Pointer to the first byte of a valid packet.
 * @return The newly allocated string or NULL if memory allocation failed
 */
char* lestr_consume_dup(uint8_t** c)
{
    uint64_t slen = leint_consume(c);
    char* str = (char*)MXS_MALLOC((slen + 1) * sizeof(char));

    if (str)
    {
        memcpy(str, *c, slen);
        str[slen] = '\0';
        *c += slen;
    }

    return str;
}

/**
 * @brief Consume a length-encoded string
 *
 * Converts length-encoded strings to character strings and advanced
 * the pointer to the next unrelated byte.
 * @param c Pointer to the start of the length-encoded string
 * @param size Pointer to a variable where the size of the string is stored
 * @return Pointer to the start of the string
 */
char* lestr_consume(uint8_t** c, size_t* size)
{
    uint64_t slen = leint_consume(c);
    *size = slen;
    char* start = (char*) *c;
    *c += slen;
    return start;
}
}
