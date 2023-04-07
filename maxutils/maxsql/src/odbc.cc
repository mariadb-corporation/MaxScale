/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxbase/ccdefs.hh>

#include <maxsql/odbc.hh>
#include <maxsql/odbc_helpers.hh>
#include <maxbase/json.hh>
#include <maxbase/log.hh>
#include <maxbase/string.hh>
#include <maxbase/assert.hh>

#include <atomic>

#include <sql.h>
#include <sqlext.h>
#include <unistd.h>

namespace
{
struct ThisUnit
{
    bool   log_statements {false};
    size_t batch_size = 1024 * 1024 * 10;
};

ThisUnit this_unit;

// Helper for converting empty string_views into null pointers
SQLCHAR* to_sql_ptr(std::string_view str)
{
    return str.empty() ? nullptr : (SQLCHAR*)str.data();
}

int fix_sql_type(int data_type)
{
    // Temporal types appear to have problems when used with the MariaDB ODBC driver. The parsing for
    // fractional TIME types as well as the use of the zero date (i.e. 0000-00-00 00:00:00) prevents the
    // values from being inserted or causes them to be converted into NULLs. Changing the datatype into
    // SQL_VARCHAR skips this part and delegates the string-to-time conversion to the database.
    if (data_type == SQL_TYPE_TIME || data_type == SQL_TYPE_TIMESTAMP || data_type == SQL_TYPE_DATE)
    {
        data_type = SQL_VARCHAR;
    }

    return data_type;
}

// Processes the given ODBC connection string into something more suitable for our use. More of a best-effort
// attempt at providing a more usable environment for the query editor in the GUI.
std::string process_connection_string(std::string str, int64_t timeout)
{
    bool is_maria = false;
    bool is_postgres = false;
    bool no_option = true;
    bool no_timeout = true;
    uint64_t option = 0;

    // The MariaDB ODBC connector reads the whole resultset into memory by default. This is obviously very
    // bad if we read a large result. This was added into C/ODBC 3.1.17 which means we still risk running
    // out of memory if an old version is used.
    uint64_t extra_flags = mxq::ODBC::FORWARDONLY | mxq::ODBC::NO_CACHE;

    // The MariaDB ODBC connector also requires this value to make multi-statement SQL work. Otherwise the
    // server will just return syntax errors for otherwise valid SQL.
    extra_flags |= mxq::ODBC::MULTI_STMT;

    for (auto tok : mxb::strtok(str, ";"))
    {
        auto [key, value] = mxb::split(tok, "=");

        if (mxb::sv_case_eq(key, "driver"))
        {
            if (value.find("libmaodbc.so") != std::string_view::npos
                || mxb::lower_case_copy(value).find("mariadb") != std::string::npos)
            {
                is_maria = true;
            }
            else if (value.find("psqlodbcw.so") != std::string_view::npos
                     || value.find("psqlodbca.so") != std::string_view::npos
                     || mxb::lower_case_copy(value).find("postgres") != std::string::npos)
            {
                is_postgres = true;
            }
            else
            {
                break;
            }
        }
        else if (mxb::sv_case_eq(key, "option"))
        {
            option = strtoul(std::string(value).c_str(), nullptr, 10);

            if ((option & extra_flags) == extra_flags)
            {
                // All the required flags are in the OPTIONS value
                no_option = false;
            }
        }

        else if (mxb::sv_case_eq(key, "conn_timeout"))
        {
            no_timeout = false;
        }
    }

    if (is_maria)
    {
        if (no_option)
        {
            str += ";OPTION=" + std::to_string(option | extra_flags);
        }

        if (no_timeout && timeout > 0)
        {
            // The MariaDB ODBC driver currently does not support timeouts set via the ODBC APIs. This means
            // we'll have to inject it into the connection string.
            str += ";CONN_TIMEOUT=" + std::to_string(timeout);
        }
    }
    else if (is_postgres)
    {
        // The Postgres ODBC driver by default wraps all SQL statements in their own SAVEPOINT commands in
        // order to be able to roll them back. We don't want them as they interfere with the
        // pg_export_snapshot() functionality. Postgres 7.4 implemented the protocol version 3 and the -0 at
        // the end of the Protocol option is what disables the SAVEPOINT functionality.
        str += ";Protocol=7.4-0";
        // It also emulates cursors by default which end up causing the whole resultset to be read
        // into memory. This would cause MaxScale to run out of memory so we need to use real cursors.
        // To make it a little bit faster, fetch 1000 rows instead of the default 100 rows.
        str += ";UseDeclareFetch=1;Fetch=1000";
        // Disable parsing in the driver, let the server deal with everything.
        str += ";Parse=0";
        // Prefer SSL if it's available
        str += ";SSLMode=prefer";
    }

    return str;
}
}

namespace maxsql
{
class ODBCImp : public Output
{
public:
    ODBCImp(std::string dsn, std::chrono::seconds timeout);

    ~ODBCImp();

    bool connect();

    void disconnect();

    const std::string& error() const;

    int errnum() const;

    const std::string& sqlstate() const;

    bool query(const std::string& sql, Output* output);

    bool prepare(const std::string& sql);

    bool unprepare();

    bool execute(Output* output);

    bool commit(int mode);

    int num_columns();

    int num_params();

    void set_row_limit(size_t limit);

    void                 set_query_timeout(std::chrono::seconds timeout);
    std::chrono::seconds query_timeout() const;

    std::string get_string_info(int type) const;

    void cancel();

    std::map<std::string, std::map<std::string, std::string>> drivers();

    std::optional<TextResult::Result>
    columns(std::string_view catalog, std::string_view schema, std::string_view table);

    std::optional<TextResult::Result>
    statistics(std::string_view catalog, std::string_view schema, std::string_view table);

    std::optional<TextResult::Result>
    primary_keys(std::string_view catalog, std::string_view schema, std::string_view table);

    std::optional<TextResult::Result>
    foreign_keys(std::string_view catalog, std::string_view schema, std::string_view table);

    bool ok_result(int64_t rows_affected, int64_t warnings) override;
    bool resultset_start(const std::vector<ColumnInfo>& metadata) override;
    bool resultset_rows(const std::vector<ColumnInfo>& metadata, ResultBuffer& res,
                        uint64_t rows_fetched) override;
    bool resultset_end(bool ok, bool complete) override;
    bool error_result(int errnum, const std::string& errmsg, const std::string& sqlstate) override;

private:
    using DiagRec = std::tuple<int, std::string, std::string>;

    template<class Hndl>
    std::vector<DiagRec> get_diag_recs(int hndl_type, Hndl hndl)
    {
        std::vector<DiagRec> records;
        SQLLEN n = 0;
        SQLRETURN ret = SQLGetDiagField(hndl_type, hndl, 0, SQL_DIAG_NUMBER, &n, 0, 0);

        for (int i = 0; i < n; i++)
        {
            SQLCHAR sqlstate[6];
            SQLCHAR msg[SQL_MAX_MESSAGE_LENGTH];
            SQLINTEGER native_error;
            SQLSMALLINT msglen = 0;

            if (SQLGetDiagRec(hndl_type, hndl, i + 1, sqlstate, &native_error,
                              msg, sizeof(msg), &msglen) != SQL_NO_DATA)
            {
                records.emplace_back(native_error,
                                     std::string_view((const char*)msg, msglen),
                                     (const char*)sqlstate);
            }
        }

        return records;
    }

    template<class Hndl>
    void get_error(int hndl_type, Hndl hndl)
    {
        if (auto errs = get_diag_recs(hndl_type, hndl); !errs.empty())
        {
            std::tie(m_errnum, m_error, m_sqlstate) = std::move(errs.back());
        }
    }

    template<class T>
    bool get_int_attr(int col, int attr, T* t)
    {
        bool ok = false;
        long value = 0;

        if (SQL_SUCCEEDED(SQLColAttribute(m_stmt, col, attr, nullptr, 0, nullptr, &value)))
        {
            *t = value;
            ok = true;
        }

        return ok;
    }

    void log_statement(const std::string& sql)
    {
        if (this_unit.log_statements)
        {
            char drv[256];
            SQLSMALLINT drvlen = 0;
            SQLGetInfo(m_conn, SQL_DRIVER_NAME, drv, sizeof(drv), &drvlen);
            drv[drvlen] = '\0';
            MXB_NOTICE("SQL(%s): \"%s\"", drv, sql.c_str());
        }
    }

    bool                    process_response(SQLRETURN ret, Output* handler);
    std::vector<ColumnInfo> get_headers(int columns);
    std::pair<bool, bool>   get_normal_result(int columns, Output* handler);
    std::pair<bool, bool>   get_batch_result(int columns, Output* handler);
    bool                    data_truncation();
    bool                    can_batch();
    bool                    canceled() const;
    void                    clear_errors();

    std::optional<TextResult::Result> get_catalog_result(SQLRETURN ret, size_t min_num_fields);

    SQLHENV     m_env {SQL_NULL_HANDLE};
    SQLHDBC     m_conn {SQL_NULL_HANDLE};
    SQLHSTMT    m_stmt {SQL_NULL_HANDLE};
    std::string m_dsn;
    std::string m_error;
    std::string m_sqlstate;
    int         m_errnum = 0;
    size_t      m_row_limit = 0;

    std::chrono::seconds    m_timeout;
    std::atomic<bool>       m_canceled {false};
    std::vector<ColumnInfo> m_columns;
};

ResultBuffer::ResultBuffer(const std::vector<ColumnInfo>& infos, size_t row_limit)
{
    size_t row_size = 0;

    for (const auto& i : infos)
    {
        row_size += buffer_size(i);
    }

    mxb_assert(row_size > 0);
    row_count = this_unit.batch_size / row_size;

    if (row_limit)
    {
        row_count = std::min(row_limit, row_count);
    }

    mxb_assert(row_count > 0);
    row_status.resize(row_count);
    columns.reserve(infos.size());

    for (const auto& i : infos)
    {
        columns.emplace_back(row_count, buffer_size(i), sql_to_c_type(i), i.data_type);
    }
}

size_t ResultBuffer::buffer_size(const ColumnInfo& c) const
{
    switch (sql_to_c_type(c))
    {
    case SQL_C_BIT:
    case SQL_C_UTINYINT:
        return sizeof(SQLCHAR);

    case SQL_C_STINYINT:
        return sizeof(SQLSCHAR);

    case SQL_C_USHORT:
        return sizeof(SQLUSMALLINT);

    case SQL_C_SSHORT:
        return sizeof(SQLSMALLINT);

    case SQL_C_ULONG:
        return sizeof(SQLUINTEGER);

    case SQL_C_SLONG:
        return sizeof(SQLINTEGER);

    case SQL_C_UBIGINT:
        return sizeof(SQLUBIGINT);

    case SQL_C_SBIGINT:
        return sizeof(SQLBIGINT);

    case SQL_C_DOUBLE:
        return sizeof(SQLDOUBLE);

        // Treat everything else as a string, keeps things simple. Also keep the buffer smaller than 1Mib,
        // some varchars seems to be blobs in reality.
    default:
        return std::min(1024UL * 1024, std::max(c.buffer_size, c.size) + 1);
    }
}

int ResultBuffer::sql_to_c_type(const ColumnInfo& c) const
{
    switch (c.data_type)
    {
    case SQL_TINYINT:
        return c.is_unsigned ? SQL_C_UTINYINT : SQL_C_STINYINT;

    case SQL_SMALLINT:
        return c.is_unsigned ? SQL_C_USHORT : SQL_C_SSHORT;

    case SQL_INTEGER:
        return c.is_unsigned ? SQL_C_ULONG : SQL_C_SLONG;

    case SQL_BIGINT:
        return c.is_unsigned ? SQL_C_UBIGINT : SQL_C_SBIGINT;

        // Connector/ODBC doesn't handle SQL_REAL correctly: https://jira.mariadb.org/browse/ODBC-374
        // We have to use a SQLDOUBLE to store all floating point types to make sure we don't crash when
        // inserting data into MariaDB.
    case SQL_REAL:
    case SQL_FLOAT:
    case SQL_DOUBLE:
        return SQL_C_DOUBLE;

    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
        return SQL_C_BINARY;

    case SQL_BIT:
        return SQL_C_BIT;

        // String, date, time et cetera. Keeps things simple as DATETIME structs are a little messy.
    default:
        return SQL_C_CHAR;
    }
}

bool ResultBuffer::Column::is_null(int row) const
{
    return indicators[row] == SQL_NULL_DATA;
}

std::string ResultBuffer::Column::to_string(int row) const
{
    std::string rval;
    const uint8_t* ptr = buffers.data() + buffer_size * row;
    const SQLLEN len = *(indicators.data() + row);

    if (len == SQL_NULL_DATA)
    {
        return "<NULL>";
    }

    switch (buffer_type)
    {
    case SQL_C_BIT:
    case SQL_C_UTINYINT:
        rval = std::to_string(*reinterpret_cast<const SQLCHAR*>(ptr));
        break;

    case SQL_C_STINYINT:
        rval = std::to_string(*reinterpret_cast<const SQLSCHAR*>(ptr));
        break;

    case SQL_C_USHORT:
        rval = std::to_string(*reinterpret_cast<const SQLUSMALLINT*>(ptr));
        break;

    case SQL_C_SSHORT:
        rval = std::to_string(*reinterpret_cast<const SQLSMALLINT*>(ptr));
        break;

    case SQL_C_ULONG:
        rval = std::to_string(*reinterpret_cast<const SQLUINTEGER*>(ptr));
        break;

    case SQL_C_SLONG:
        rval = std::to_string(*reinterpret_cast<const SQLINTEGER*>(ptr));
        break;

    case SQL_C_UBIGINT:
        rval = std::to_string(*reinterpret_cast<const SQLUBIGINT*>(ptr));
        break;

    case SQL_C_SBIGINT:
        rval = std::to_string(*reinterpret_cast<const SQLBIGINT*>(ptr));
        break;

    case SQL_C_FLOAT:
        mxb_assert_message(!true, "Floats shouldn't be used, they are broken in C/ODBC");
        rval = std::to_string(*reinterpret_cast<const SQLREAL*>(ptr));
        break;

    case SQL_C_DOUBLE:
        rval = std::to_string(*reinterpret_cast<const SQLDOUBLE*>(ptr));
        break;

    default:
        rval.assign((const char*)ptr, len);
        break;
    }

    return rval;
}

json_t* ResultBuffer::Column::to_json(int row) const
{
    json_t* rval = nullptr;
    const uint8_t* ptr = buffers.data() + buffer_size * row;
    const SQLLEN len = *(indicators.data() + row);

    if (len == SQL_NULL_DATA)
    {
        return json_null();
    }

    switch (buffer_type)
    {
    case SQL_C_BIT:
    case SQL_C_UTINYINT:
        rval = json_integer(*reinterpret_cast<const SQLCHAR*>(ptr));
        break;

    case SQL_C_STINYINT:
        rval = json_integer(*reinterpret_cast<const SQLSCHAR*>(ptr));
        break;

    case SQL_C_USHORT:
        rval = json_integer(*reinterpret_cast<const SQLUSMALLINT*>(ptr));
        break;

    case SQL_C_SSHORT:
        rval = json_integer(*reinterpret_cast<const SQLSMALLINT*>(ptr));
        break;

    case SQL_C_ULONG:
        rval = json_integer(*reinterpret_cast<const SQLUINTEGER*>(ptr));
        break;

    case SQL_C_SLONG:
        rval = json_integer(*reinterpret_cast<const SQLINTEGER*>(ptr));
        break;

    case SQL_C_UBIGINT:
        rval = json_integer(*reinterpret_cast<const SQLUBIGINT*>(ptr));
        break;

    case SQL_C_SBIGINT:
        rval = json_integer(*reinterpret_cast<const SQLBIGINT*>(ptr));
        break;

    case SQL_C_FLOAT:
        mxb_assert_message(!true, "Floats shouldn't be used, they are broken in C/ODBC");
        rval = json_real(*reinterpret_cast<const SQLREAL*>(ptr));
        break;

    case SQL_C_DOUBLE:
        rval = json_real(*reinterpret_cast<const SQLDOUBLE*>(ptr));
        break;

    default:
        if (data_type == SQL_NUMERIC || data_type == SQL_DECIMAL)
        {
            std::string str(ptr, ptr + len);
            char* end = nullptr;
            double val = strtod(str.c_str(), &end);

            if (end == str.c_str() + str.length())
            {
                rval = json_real(val);
            }
            else
            {
                rval = json_stringn((const char*)ptr, len);
            }
        }
        else
        {
            rval = json_stringn((const char*)ptr, len);
        }

        break;
    }

    mxb_assert(rval);
    return rval;
}

bool JsonResult::ok_result(int64_t rows_affected, int64_t warnings)
{
    mxb::Json obj(mxb::Json::Type::OBJECT);
    obj.set_int("last_insert_id", 0);
    obj.set_int("warnings", warnings);
    obj.set_int("affected_rows", rows_affected);
    m_result.add_array_elem(std::move(obj));
    return true;
}

bool JsonResult::resultset_start(const std::vector<ColumnInfo>& metadata)
{
    m_data = mxb::Json{mxb::Json::Type::ARRAY};
    m_fields = mxb::Json{mxb::Json::Type::ARRAY};

    for (const auto& col : metadata)
    {
        m_fields.add_array_elem(mxb::Json(json_string(col.name.c_str()), mxb::Json::RefType::STEAL));
    }

    return true;
}

bool JsonResult::resultset_rows(const std::vector<ColumnInfo>& metadata,
                                ResultBuffer& res,
                                uint64_t rows_fetched)
{
    int columns = metadata.size();

    for (uint64_t i = 0; i < rows_fetched; i++)
    {
        mxb::Json row(mxb::Json::Type::ARRAY);

        if (res.row_status[i] == SQL_ROW_SUCCESS || res.row_status[i] == SQL_ROW_SUCCESS_WITH_INFO)
        {
            for (int c = 0; c < columns; c++)
            {
                if (!res.columns[c].is_null(i))
                {
                    row.add_array_elem(mxb::Json(res.columns[c].to_json(i), mxb::Json::RefType::STEAL));
                }
            }

            m_data.add_array_elem(std::move(row));
        }
    }

    return true;
}

bool JsonResult::resultset_end(bool ok, bool complete)
{
    mxb::Json obj(mxb::Json::Type::OBJECT);
    obj.set_object("fields", std::move(m_fields));
    obj.set_object("data", std::move(m_data));
    obj.set_bool("complete", complete);
    m_result.add_array_elem(std::move(obj));
    return true;
}

bool JsonResult::error_result(int errnum, const std::string& errmsg, const std::string& sqlstate)
{
    mxb::Json obj(mxb::Json::Type::OBJECT);
    obj.set_int("errno", errnum);
    obj.set_string("message", errmsg);
    obj.set_string("sqlstate", sqlstate);
    m_result.add_array_elem(std::move(obj));
    return true;
}

bool TextResult::ok_result(int64_t rows_affected, int64_t warnings)
{
    return true;
}

bool TextResult::resultset_start(const std::vector<ColumnInfo>& metadata)
{
    m_data = Result{};
    return true;
}

bool TextResult::resultset_rows(const std::vector<ColumnInfo>& metadata,
                                ResultBuffer& res,
                                uint64_t rows_fetched)
{
    int columns = metadata.size();

    for (uint64_t i = 0; i < rows_fetched; i++)
    {
        Row row(columns);

        if (res.row_status[i] == SQL_ROW_SUCCESS || res.row_status[i] == SQL_ROW_SUCCESS_WITH_INFO)
        {
            for (int c = 0; c < columns; c++)
            {
                if (!res.columns[c].is_null(i))
                {
                    row[c] = res.columns[c].to_string(i);
                }
                else
                {
                    row[c] = "NULL";
                }
            }

            m_data.push_back(std::move(row));
        }
    }

    return true;
}

bool TextResult::resultset_end(bool ok, bool complete)
{
    m_result.push_back(std::move(m_data));
    return true;
}

bool TextResult::error_result(int errnum, const std::string& errmsg, const std::string& sqlstate)
{
    // Ignore errors, they're available in the ODBC class so there's no need to duplicate them here.
    return true;
}

std::optional<std::string> TextResult::get_field(size_t field, size_t row, size_t result) const
{
    try
    {
        return m_result.at(result).at(row).at(field).value();
    }
    catch (const std::exception& e)
    {
        return {};      // Value not present
    }
}

ODBCImp::ODBCImp(std::string dsn, std::chrono::seconds timeout)
    : m_dsn(process_connection_string(dsn, timeout.count()))
    , m_timeout(timeout)
{
    if (SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_env)))
    {
        SQLSetEnvAttr(m_env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
        // The DBC handler must be allocated after the ODBC version is set, otherwise the SQLConnect
        // function returns SQL_INVALID_HANDLE.
        if (SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, m_env, &m_conn)))
        {
            SQLSetConnectAttr(m_conn, SQL_ATTR_CONNECTION_TIMEOUT, (SQLPOINTER)timeout.count(), 0);
            SQLSetConnectAttr(m_conn, SQL_ATTR_LOGIN_TIMEOUT, (SQLPOINTER)timeout.count(), 0);
        }
    }
}

ODBCImp::~ODBCImp()
{
    if (m_stmt != SQL_NULL_HANDLE)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, m_stmt);
    }

    if (m_conn != SQL_NULL_HANDLE)
    {
        SQLDisconnect(m_conn);
        SQLFreeHandle(SQL_HANDLE_DBC, m_conn);
    }

    if (m_env != SQL_NULL_HANDLE)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, m_env);
    }
}

bool ODBCImp::connect()
{
    if (m_env == SQL_NULL_HANDLE || m_conn == SQL_NULL_HANDLE)
    {
        return false;
    }

    SQLCHAR outbuf[1024];
    SQLSMALLINT s2len;
    SQLRETURN ret = SQLDriverConnect(m_conn, nullptr, (SQLCHAR*)m_dsn.c_str(), m_dsn.size(),
                                     outbuf, sizeof(outbuf), &s2len, SQL_DRIVER_NOPROMPT);

    if (ret == SQL_ERROR)
    {
        get_error(SQL_HANDLE_DBC, m_conn);
    }
    else
    {
        ret = SQLAllocHandle(SQL_HANDLE_STMT, m_conn, &m_stmt);

        if (ret == SQL_ERROR)
        {
            get_error(SQL_HANDLE_DBC, m_conn);
        }
        else
        {
            SQLSetStmtAttr(m_stmt, SQL_ATTR_QUERY_TIMEOUT, (SQLPOINTER)m_timeout.count(), 0);
            SQLSetStmtAttr(m_stmt, SQL_ATTR_CURSOR_TYPE, (SQLPOINTER)SQL_CURSOR_FORWARD_ONLY, 0);
            SQLSetStmtAttr(m_stmt, SQL_ATTR_CONCURRENCY, (SQLPOINTER)SQL_CONCUR_READ_ONLY, 0);
        }
    }

    return SQL_SUCCEEDED(ret);
}

void ODBCImp::disconnect()
{
    SQLDisconnect(m_conn);

    if (m_stmt != SQL_NULL_HANDLE)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, m_stmt);
        m_stmt = SQL_NULL_HANDLE;
    }
}

const std::string& ODBCImp::error() const
{
    return m_error;
}

int ODBCImp::errnum() const
{
    return m_errnum;
}

const std::string& ODBCImp::sqlstate() const
{
    return m_sqlstate;
}

std::map<std::string, std::map<std::string, std::string>> ODBCImp::drivers()
{
    std::map<std::string, std::map<std::string, std::string>> rval;
    SQLCHAR drv[512];
    std::vector<SQLCHAR> attr(1024);
    SQLSMALLINT drv_sz = 0;
    SQLSMALLINT attr_sz = 0;
    SQLUSMALLINT dir = SQL_FETCH_FIRST;
    SQLRETURN ret;

    while (SQL_SUCCEEDED(ret = SQLDrivers(m_env, dir, drv, sizeof(drv), &drv_sz,
                                          attr.data(), attr.size(), &attr_sz)))
    {
        if (ret == SQL_SUCCESS_WITH_INFO && data_truncation())
        {
            // The buffer was too small, need more space
            attr.resize(attr.size() * 2);
            dir = SQL_FETCH_FIRST;
        }
        else
        {
            dir = SQL_FETCH_NEXT;
            std::map<std::string, std::string> values;

            // The values are separated by nulls and terminated by nulls. Once we find an empty string, we've
            // reached the end of the attribute list.
            for (char* ptr = (char*)attr.data(); *ptr; ptr += strlen(ptr) + 1)
            {
                if (auto tok = mxb::strtok(ptr, "="); tok.size() >= 2)
                {
                    values.emplace(std::move(tok[0]), std::move(tok[1]));
                }
            }

            for (auto kw : {"Driver", "Driver64"})
            {
                if (auto it = values.find(kw); it != values.end())
                {
                    // Check that the driver is actually installed. For some reason there are drivers
                    // defined by default on some systems (Fedora 36) that aren't actually installed.
                    if (access(it->second.c_str(), F_OK) == 0)
                    {
                        rval.emplace((char*)drv, std::move(values));
                        break;
                    }
                }
            }
        }
    }

    return rval;
}

std::optional<TextResult::Result>  ODBCImp::get_catalog_result(SQLRETURN ret, size_t min_num_fields)
{
    std::optional<TextResult::Result> rval;
    TextResult result;

    if (process_response(ret, &result))
    {
        const auto& rset = result.result();
        mxb_assert_message(rset.size() < 2, "Should return only one result");

        if (rset.empty() || rset[0].empty() || rset[0][0].empty())
        {
            rval = TextResult::Result{};
        }
        else
        {
            if (rset[0][0].size() >= min_num_fields)
            {
                rval = rset.front();
            }
            else
            {
                m_error = "Malformed ODBC catalog result";
            }
        }
    }

    return rval;
}

std::optional<TextResult::Result>
ODBCImp::columns(std::string_view catalog, std::string_view schema, std::string_view table)
{
    SQLRETURN ret = SQLColumns(m_stmt,
                               to_sql_ptr(catalog), catalog.size(),
                               to_sql_ptr(schema), schema.size(),
                               to_sql_ptr(table), table.size(),
                               nullptr, 0);
    return get_catalog_result(ret, 18);
}

std::optional<TextResult::Result>
ODBCImp::statistics(std::string_view catalog, std::string_view schema, std::string_view table)
{
    SQLRETURN ret = SQLStatistics(m_stmt,
                                  to_sql_ptr(catalog), catalog.size(),
                                  to_sql_ptr(schema), schema.size(),
                                  to_sql_ptr(table), table.size(),
                                  SQL_INDEX_ALL, SQL_QUICK);

    return get_catalog_result(ret, 13);
}

std::optional<TextResult::Result>
ODBCImp::primary_keys(std::string_view catalog, std::string_view schema, std::string_view table)
{
    SQLRETURN ret = SQLPrimaryKeys(m_stmt,
                                   to_sql_ptr(catalog), catalog.size(),
                                   to_sql_ptr(schema), schema.size(),
                                   to_sql_ptr(table), table.size());

    return get_catalog_result(ret, 6);
}

std::optional<TextResult::Result>
ODBCImp::foreign_keys(std::string_view catalog, std::string_view schema, std::string_view table)
{
    SQLRETURN ret = SQLForeignKeys(m_stmt,
                                   nullptr, 0, nullptr, 0, nullptr, 0,
                                   to_sql_ptr(catalog), catalog.size(),
                                   to_sql_ptr(schema), schema.size(),
                                   to_sql_ptr(table), table.size()
                                   );

    return get_catalog_result(ret, 14);
}

bool ODBCImp::canceled() const
{
    return m_canceled.load(std::memory_order_relaxed);
}

void ODBCImp::clear_errors()
{
    m_canceled.store(false, std::memory_order_relaxed);
    std::tie(m_errnum, m_error, m_sqlstate) = std::make_tuple(0, "", "");
}

bool ODBCImp::query(const std::string& query, Output* output)
{
    log_statement(query);
    clear_errors();
    SQLRETURN ret = SQLExecDirect(m_stmt, (SQLCHAR*)query.c_str(), query.size());
    return process_response(ret, output);
}

bool ODBCImp::prepare(const std::string& query)
{
    log_statement(query);
    clear_errors();
    SQLRETURN ret = SQLPrepare(m_stmt, (SQLCHAR*)query.c_str(), query.size());

    if (ret == SQL_ERROR)
    {
        get_error(SQL_HANDLE_STMT, m_stmt);
    }

    return SQL_SUCCEEDED(ret);
}

bool ODBCImp::unprepare()
{
    clear_errors();
    SQLRETURN ret = SQLCloseCursor(m_stmt);

    if (ret == SQL_ERROR)
    {
        get_error(SQL_HANDLE_STMT, m_stmt);
    }

    return SQL_SUCCEEDED(ret);
}

int ODBCImp::num_columns()
{
    SQLSMALLINT params = -1;
    SQLRETURN ret = SQLNumResultCols(m_stmt, &params);
    return params;
}

int ODBCImp::num_params()
{
    SQLSMALLINT params = -1;
    SQLRETURN ret = SQLNumParams(m_stmt, &params);
    return params;
}

bool ODBCImp::execute(Output* output)
{
    clear_errors();
    SQLRETURN ret = SQLExecute(m_stmt);
    return process_response(ret, output);
}

bool ODBCImp::commit(int mode)
{
    mxb_assert(mode == SQL_COMMIT || mode == SQL_ROLLBACK);
    SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, m_conn, mode);

    if (ret == SQL_ERROR)
    {
        get_error(SQL_HANDLE_STMT, m_stmt);
    }

    return SQL_SUCCEEDED(ret);
}

bool ODBCImp::ok_result(int64_t rows_affected, int64_t warnings)
{
    mxb_assert_message(!true, "SELECT should not generate an OK result.");
    return false;
}

bool ODBCImp::resultset_start(const std::vector<ColumnInfo>& metadata)
{
    return m_error.empty();
}

bool ODBCImp::resultset_rows(const std::vector<ColumnInfo>& metadata, ResultBuffer& res,
                             uint64_t rows_fetched)
{
    SQLLEN params_processed = 0;
    SQLSetStmtAttr(m_stmt, SQL_ATTR_PARAM_BIND_TYPE, (void*)SQL_PARAM_BIND_BY_COLUMN, 0);
    SQLSetStmtAttr(m_stmt, SQL_ATTR_PARAMSET_SIZE, (void*)rows_fetched, 0);
    SQLSetStmtAttr(m_stmt, SQL_ATTR_PARAM_STATUS_PTR, res.row_status.data(), 0);
    SQLSetStmtAttr(m_stmt, SQL_ATTR_PARAMS_PROCESSED_PTR, &params_processed, 0);

    for (size_t i = 0; i < metadata.size(); i++)
    {
        SQLBindParameter(m_stmt, i + 1, SQL_PARAM_INPUT, res.columns[i].buffer_type,
                         fix_sql_type(metadata[i].data_type), metadata[i].size, metadata[i].digits,
                         (SQLPOINTER*)res.columns[i].buffers.data(), res.columns[i].buffer_size,
                         res.columns[i].indicators.data());
    }

    SQLRETURN ret = SQLExecute(m_stmt);

    if (ret == SQL_ERROR)
    {
        get_error(SQL_HANDLE_STMT, m_stmt);
    }
    else if (canceled())
    {
        ret = SQL_ERROR;
        m_errnum = 1927;
        m_error = "Connection was killed";
        m_sqlstate = "HY000";
    }

    return SQL_SUCCEEDED(ret);
}

bool ODBCImp::resultset_end(bool ok, bool complete)
{
    SQLFreeStmt(m_stmt, SQL_UNBIND);
    SQLFreeStmt(m_stmt, SQL_RESET_PARAMS);
    return commit(ok ? SQL_COMMIT : SQL_ROLLBACK);
}


bool ODBCImp::error_result(int errnum, const std::string& errmsg, const std::string& sqlstate)
{
    // Shouldn't happen unless the resultset ended in an error.
    MXB_INFO("Error result while loading data: #%s %d, %s", sqlstate.c_str(), errnum, errmsg.c_str());
    return false;
}

void ODBCImp::set_row_limit(size_t limit)
{
    m_row_limit = limit;
}

void ODBCImp::set_query_timeout(std::chrono::seconds timeout)
{
    m_timeout = timeout;

    if (m_stmt != SQL_NULL_HANDLE)
    {
        SQLSetStmtAttr(m_stmt, SQL_ATTR_QUERY_TIMEOUT, (SQLPOINTER)m_timeout.count(), 0);
    }
}

std::chrono::seconds ODBCImp::query_timeout() const
{
    return m_timeout;
}

std::string ODBCImp::get_string_info(int type) const
{
    char buf[512];
    SQLSMALLINT buflen = 0;
    SQLGetInfo(m_conn, type, buf, sizeof(buf), &buflen);
    buf[buflen] = '\0';
    return buf;
}

void ODBCImp::cancel()
{
    m_canceled.store(true, std::memory_order_relaxed);
    SQLCancel(m_stmt);
}

bool ODBCImp::process_response(SQLRETURN ret, Output* handler)
{
    mxb_assert(handler);
    bool ok = false;

    if (SQL_SUCCEEDED(ret))
    {
        ok = true;

        do
        {
            SQLSMALLINT columns = 0;
            SQLNumResultCols(m_stmt, &columns);

            if (columns == 0)
            {
                SQLLEN rowcount = 0;
                SQLRowCount(m_stmt, &rowcount);
                // C/ODBC doesn't seem to return warnings at all, other drivers do return them.
                int64_t warnings = ret == SQL_SUCCESS_WITH_INFO ?
                    get_diag_recs(SQL_HANDLE_STMT, m_stmt).size() : 0;

                if (!handler->ok_result(rowcount, warnings))
                {
                    MXB_DEBUG("Output failed to process OK result");
                    ok = false;
                }
            }
            else if (columns > 0)
            {
                m_columns = get_headers(columns);

                if (handler->resultset_start(m_columns))
                {
                    bool complete;
                    std::tie(ok, complete) = can_batch() ?
                        get_batch_result(columns, handler) :
                        get_normal_result(columns, handler);

                    if (!handler->resultset_end(ok, complete))
                    {
                        MXB_DEBUG("Output failed to process resultset end");
                        ok = false;
                    }
                }
                else
                {
                    MXB_DEBUG("Output failed to process resultset start");
                    ok = false;
                }
            }
        }
        while (ok && !canceled() && SQL_SUCCEEDED(ret = SQLMoreResults(m_stmt)));

        if (ret == SQL_ERROR)
        {
            get_error(SQL_HANDLE_STMT, m_stmt);

            if (!handler->error_result(m_errnum, m_error, m_sqlstate))
            {
                MXB_DEBUG("Output failed to process error result");
                ok = false;
            }
        }

        SQLCloseCursor(m_stmt);
    }
    else if (ret == SQL_ERROR)
    {
        get_error(SQL_HANDLE_STMT, m_stmt);

        if (!handler->error_result(m_errnum, m_error, m_sqlstate))
        {
            MXB_DEBUG("Output failed to process error result");
            ok = false;
        }
    }

    return ok;
}

bool ODBCImp::data_truncation()
{
    constexpr std::string_view truncated = "01004";
    SQLLEN n = 0;
    SQLGetDiagField(SQL_HANDLE_STMT, m_stmt, 0, SQL_DIAG_NUMBER, &n, 0, 0);

    for (int i = 0; i < n; i++)
    {
        SQLCHAR sqlstate[6];
        SQLCHAR msg[SQL_MAX_MESSAGE_LENGTH];
        SQLINTEGER native_error;
        SQLSMALLINT msglen = 0;

        if (SQLGetDiagRec(SQL_HANDLE_STMT, m_stmt, i + 1, sqlstate, &native_error,
                          msg, sizeof(msg), &msglen) != SQL_NO_DATA)
        {
            if ((const char*)sqlstate == truncated)
            {
                return true;
            }
        }
    }

    return false;
}

std::vector<ColumnInfo> ODBCImp::get_headers(int columns)
{
    std::vector<ColumnInfo> cols;
    cols.reserve(columns);

    for (SQLSMALLINT i = 0; i < columns; i++)
    {
        char name[256] = "";
        SQLSMALLINT namelen = 0;
        SQLSMALLINT data_type;
        SQLULEN colsize;
        SQLSMALLINT digits;
        SQLSMALLINT nullable;

        SQLRETURN ret = SQLDescribeCol(m_stmt, i + 1, (SQLCHAR*)name, sizeof(name), &namelen,
                                       &data_type, &colsize, &digits, &nullable);

        if (SQL_SUCCEEDED(ret))
        {
            ColumnInfo info;
            info.name = name;
            info.size = colsize;
            info.data_type = data_type;
            info.digits = digits;
            info.nullable = nullable;

            if (!get_int_attr(i + 1, SQL_DESC_OCTET_LENGTH, &info.buffer_size))
            {
                get_error(SQL_HANDLE_STMT, m_stmt);
                SQLCloseCursor(m_stmt);
                return {};
            }

            SQLLEN is_unsigned = SQL_FALSE;

            if (!get_int_attr(i + 1, SQL_DESC_UNSIGNED, &is_unsigned))
            {
                get_error(SQL_HANDLE_STMT, m_stmt);
                SQLCloseCursor(m_stmt);
                return {};
            }

            info.is_unsigned = is_unsigned;

            cols.push_back(std::move(info));
        }
        else if (ret == SQL_ERROR)
        {
            get_error(SQL_HANDLE_STMT, m_stmt);
            SQLCloseCursor(m_stmt);
            return {};
        }
    }

    return cols;
}

std::pair<bool, bool> ODBCImp::get_normal_result(int columns, Output* handler)
{
    SQLRETURN ret = SQL_SUCCESS;
    ResultBuffer res(m_columns, 1);

    bool ok = true;

    SQLULEN rows_fetched = 0;
    SQLSetStmtAttr(m_stmt, SQL_ATTR_ROW_ARRAY_SIZE, (void*)res.row_count, 0);
    SQLSetStmtAttr(m_stmt, SQL_ATTR_ROWS_FETCHED_PTR, &rows_fetched, 0);
    SQLSetStmtAttr(m_stmt, SQL_ATTR_ROW_STATUS_PTR, res.row_status.data(), 0);

    size_t total_rows = 0;
    bool below_limit = true;

    while (ok && !canceled() && SQL_SUCCEEDED(ret = SQLFetch(m_stmt)))
    {
        if (m_row_limit > 0 && ++total_rows > m_row_limit)
        {
            below_limit = false;
            break;
        }

        for (SQLSMALLINT i = 0; i < columns; i++)
        {
            auto& c = res.columns[i];
            ret = SQLGetData(m_stmt, i + 1, c.buffer_type, c.buffers.data(), c.buffers.size(),
                             c.indicators.data());

            while (ret == SQL_SUCCESS_WITH_INFO && data_truncation())
            {
                auto old_size = c.buffers.size() - 1;   // Minus one since these are null-terminated strings
                c.buffers.resize(c.indicators.front());
                c.buffer_size = c.buffers.size();
                ret = SQLGetData(m_stmt, i + 1, c.buffer_type, c.buffers.data() + old_size,
                                 c.buffers.size() - old_size, c.indicators.data());
            }

            if (ret == SQL_ERROR)
            {
                ok = false;
            }
        }

        if (ok && !handler->resultset_rows(m_columns, res, 1))
        {
            ok = false;
        }
    }

    if (ret == SQL_ERROR)
    {
        get_error(SQL_HANDLE_STMT, m_stmt);
        ok = false;
    }

    return {ok, below_limit};
}

std::pair<bool, bool> ODBCImp::get_batch_result(int columns, Output* handler)
{
    ResultBuffer res(m_columns, m_row_limit);

    SQLULEN rows_fetched = 0;
    SQLSetStmtAttr(m_stmt, SQL_ATTR_ROW_BIND_TYPE, (void*)SQL_BIND_BY_COLUMN, 0);
    SQLSetStmtAttr(m_stmt, SQL_ATTR_ROW_ARRAY_SIZE, (void*)res.row_count, 0);
    SQLSetStmtAttr(m_stmt, SQL_ATTR_ROWS_FETCHED_PTR, &rows_fetched, 0);
    SQLSetStmtAttr(m_stmt, SQL_ATTR_ROW_STATUS_PTR, res.row_status.data(), 0);

    SQLRETURN ret = SQL_SUCCESS;
    bool ok = true;

    for (int i = 0; i < columns; i++)
    {
        ret = SQLBindCol(m_stmt, i + 1, res.columns[i].buffer_type, res.columns[i].buffers.data(),
                         res.columns[i].buffer_size, res.columns[i].indicators.data());

        if (!SQL_SUCCEEDED(ret))
        {
            ok = false;
            break;
        }
    }

    size_t total_rows = 0;
    bool below_limit = true;

    while (ok && below_limit && !canceled() && SQL_SUCCEEDED(ret = SQLFetch(m_stmt)))
    {
        total_rows += rows_fetched;

        if (m_row_limit > 0 && total_rows > m_row_limit)
        {
            rows_fetched = m_row_limit - (total_rows - rows_fetched);
            below_limit = false;

            if (rows_fetched == 0)
            {
                continue;
            }
        }

        if (!handler->resultset_rows(m_columns, res, rows_fetched))
        {
            MXB_DEBUG("Output failed to process resultset row at offset %lu", total_rows);
            ok = false;
        }
    }

    if (ret == SQL_ERROR)
    {
        get_error(SQL_HANDLE_STMT, m_stmt);

        if (m_error.empty())
        {
            get_error(SQL_HANDLE_DBC, m_conn);
        }

        ok = false;
    }

    SQLFreeStmt(m_stmt, SQL_UNBIND);

    return {ok, below_limit};
}

bool ODBCImp::can_batch()
{
    for (const auto& i : m_columns)
    {
        size_t buffer_size = 0;

        switch (i.data_type)
        {
        // If the result has LOBs in it, the data should be retrieved one row at a time using
        // SQLGetData instead of using an array to fetch multiple rows at a time.
        case SQL_WLONGVARCHAR:
        case SQL_LONGVARCHAR:
        case SQL_LONGVARBINARY:
            return i.size < 16384;

        default:
            // Around the maximum value of a VARCHAR field. Anything bigger than this should be read one value
            // at a time to reduce memory usage.
            constexpr size_t MAX_CHUNK_SIZE = 65536;

            if (i.size == 0 || i.size > MAX_CHUNK_SIZE)
            {
                // The driver either doesn't know how big the value is or it is way too large to be batched.
                return false;
            }
        }
    }

    return true;
}

ODBC::ODBC(std::string dsn, std::chrono::seconds timeout)
    : m_imp(std::make_unique<mxq::ODBCImp>(std::move(dsn), timeout))
{
}

ODBC::~ODBC()
{
}

ODBC::ODBC(ODBC&& other)
{
    m_imp = move(other.m_imp);
}

ODBC& ODBC::operator=(ODBC&& other)
{
    m_imp = move(other.m_imp);
    return *this;
}

bool ODBC::connect()
{
    return m_imp->connect();
}

void ODBC::disconnect()
{
    m_imp->disconnect();
}

const std::string& ODBC::error() const
{
    return m_imp->error();
}

int ODBC::errnum() const
{
    return m_imp->errnum();
}

const std::string& ODBC::sqlstate() const
{
    return m_imp->sqlstate();
}

bool ODBC::query(const std::string& sql, mxq::Output* output)
{
    return m_imp->query(sql, output);
}

bool ODBC::prepare(const std::string& sql)
{
    return m_imp->prepare(sql);
}

bool ODBC::unprepare()
{
    return m_imp->unprepare();
}

bool ODBC::commit()
{
    return m_imp->commit(SQL_COMMIT);
}

bool ODBC::rollback()
{
    return m_imp->commit(SQL_ROLLBACK);
}

int ODBC::num_columns()
{
    return m_imp->num_columns();
}

int ODBC::num_params()
{
    return m_imp->num_params();
}

bool ODBC::execute(mxq::Output* output)
{
    return m_imp->execute(output);
}

Output* ODBC::as_output()
{
    return m_imp.get();
}

void ODBC::set_row_limit(size_t limit)
{
    m_imp->set_row_limit(limit);
}

void ODBC::set_query_timeout(std::chrono::seconds timeout)
{
    m_imp->set_query_timeout(timeout);
}

std::chrono::seconds ODBC::query_timeout() const
{
    return m_imp->query_timeout();
}

std::string ODBC::driver_name() const
{
    return m_imp->get_string_info(SQL_DRIVER_NAME);
}

std::string ODBC::driver_version() const
{
    return m_imp->get_string_info(SQL_DRIVER_VER);
}

void ODBC::cancel()
{
    return m_imp->cancel();
}

std::optional<TextResult::Result>
ODBC::columns(std::string_view catalog, std::string_view schema, std::string_view table)
{
    return m_imp->columns(catalog, schema, table);
}

std::optional<TextResult::Result>
ODBC::statistics(std::string_view catalog, std::string_view schema, std::string_view table)
{
    return m_imp->statistics(catalog, schema, table);
}

std::optional<TextResult::Result>
ODBC::primary_keys(std::string_view catalog, std::string_view schema, std::string_view table)
{
    return m_imp->primary_keys(catalog, schema, table);
}

std::optional<TextResult::Result>
ODBC::foreign_keys(std::string_view catalog, std::string_view schema, std::string_view table)
{
    return m_imp->foreign_keys(catalog, schema, table);
}

// static
std::map<std::string, std::map<std::string, std::string>> ODBC::drivers()
{
    // This simplifies the driver querying. We don't need a connection but we do need a valid environment
    // handle to get the drivers.
    auto tmp = std::make_unique<mxq::ODBCImp>("", 0s);
    return tmp->drivers();
}

void odbc_set_log_statements(bool enable)
{
    this_unit.log_statements = enable;
}

void odbc_set_batch_size(size_t size)
{
    this_unit.batch_size = size;
}
}
