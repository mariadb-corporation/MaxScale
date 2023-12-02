/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxsql/ccdefs.hh>

#include <optional>
#include <string>
#include <vector>
#include <map>
#include <memory>

#include <maxbase/json.hh>

namespace maxsql
{

/**
 * Sets the desired batch size for batch operations
 *
 * @param size Size in bytes
 */
void odbc_set_batch_size(size_t size);

struct ColumnInfo
{
    std::string name;               // Column name
    int         data_type {0};      // ODBC data type
    size_t      size {0};           // The size of the SQL type (e.g. Unicode characters)
    size_t      buffer_size {0};    // The "octet" size, i.e. size in bytes
    int         digits {0};         // Number of digits, zero if not applicable
    bool        nullable {false};   // If column is nullable
    bool        is_unsigned {false};// If the column is unsigned
};

struct ResultBuffer
{
    struct Column
    {
        Column(size_t row_count, size_t buffer_sz, int c_type, int sql_type)
            : buffer_size(buffer_sz)
            , buffer_type(c_type)
            , data_type(sql_type)
            , buffers(row_count * buffer_size)
            , indicators(row_count)
        {
        }

        bool        is_null(int row) const;
        std::string to_string(int row) const;
        json_t*     to_json(int row) const;

        size_t               buffer_size;   // Size of one value
        int                  buffer_type;   // ODBC C data type
        int                  data_type;     // ODBC SQL data type
        std::vector<uint8_t> buffers;       // Buffer that contains the column values
        std::vector<long>    indicators;    // Indicator values for each of the column values
    };

    ResultBuffer(const std::vector<ColumnInfo>& infos, size_t row_limit = 0);

    size_t buffer_size(const ColumnInfo& c) const;
    int    sql_to_c_type(const ColumnInfo& c) const;

    size_t                      row_count = 0;
    std::vector<Column>         columns;
    std::vector<unsigned short> row_status;
};

class Output
{
public:
    virtual ~Output() = default;

    /**
     * Called whenever an empty result (i.e. an OK packet) is received.
     *
     * @param rows_affected Number of rows affected
     * @param warnings      Number of warnings
     *
     * @return True if the OK result processing was successful
     */
    virtual bool ok_result(int64_t rows_affected, int64_t warnings) = 0;

    /**
     * Called before the first row of the resultset is read
     *
     * @param metadata Resultset metadata
     *
     * @return True if the resultset start processing was successful
     */
    virtual bool resultset_start(const std::vector<ColumnInfo>& metadata) = 0;

    /**
     * Called for each batch of rows read
     *
     * @param metadata     Resultset metadata
     * @param res          The resultset rows
     * @param rows_fetched Number of rows available in `res`
     *
     * @return True if the resultset rows processing was successful
     */
    virtual bool resultset_rows(const std::vector<ColumnInfo>& metadata,
                                ResultBuffer& res, uint64_t rows_fetched) = 0;

    /**
     * Called when the resultset ends.
     *
     * @param ok       True if the resultset was read successfully
     * @param complete True if the whole resultset was read. False if the result was truncated due to a row
     *                 number limit being hit.
     *
     * @return True if the resultset end processing is successful
     */
    virtual bool resultset_end(bool ok, bool complete) = 0;

    /**
     * Called when an error occurs
     *
     * @param errnum   Error number
     * @param errmsg   Error message
     * @param sqlstate Current SQLSTATE
     *
     * @return True if error processing was successful.
     */
    virtual bool error_result(int errnum, const std::string& errmsg, const std::string& sqlstate) = 0;
};

// Creates a mxb::Json result
struct JsonResult : public Output
{
    bool ok_result(int64_t rows_affected, int64_t warnings) override;
    bool resultset_start(const std::vector<ColumnInfo>& metadata) override;
    bool resultset_rows(const std::vector<ColumnInfo>& metadata, ResultBuffer& res,
                        uint64_t rows_fetched) override;
    bool resultset_end(bool ok, bool complete) override;
    bool error_result(int errnum, const std::string& errmsg, const std::string& sqlstate) override;

    mxb::Json result()
    {
        return m_result;
    }

private:
    mxb::Json m_result{mxb::Json::Type::ARRAY};
    mxb::Json m_data;
    mxb::Json m_fields;
};

// Creates a text result
struct TextResult : public Output
{
    // Nulls are represented as empty std::optional values
    using Value = std::optional<std::string>;
    using Row = std::vector<Value>;
    using Result = std::vector<Row>;

    bool ok_result(int64_t rows_affected, int64_t warnings) override;
    bool resultset_start(const std::vector<ColumnInfo>& metadata) override;
    bool resultset_rows(const std::vector<ColumnInfo>& metadata, ResultBuffer& res,
                        uint64_t rows_fetched) override;
    bool resultset_end(bool ok, bool complete) override;
    bool error_result(int errnum, const std::string& errmsg, const std::string& sqlstate) override;

    const std::vector<Result>& result() const
    {
        return m_result;
    }

    // Helper that extracts the given field in the resultset, if present.
    std::optional<std::string> get_field(size_t field, size_t row = 0, size_t result = 0) const;

private:
    std::vector<Result> m_result;
    Result              m_data;
};

// Discards the result
class NoResult : public Output
{
public:
    virtual bool ok_result(int64_t rows_affected, int64_t warnings) override
    {
        return true;
    }

    bool resultset_start(const std::vector<ColumnInfo>& metadata) override
    {
        return true;
    }

    bool resultset_rows(const std::vector<ColumnInfo>& metadata, ResultBuffer& res,
                        uint64_t rows_fetched) override
    {
        return true;
    }

    bool resultset_end(bool ok, bool complete) override
    {
        if (!ok)
        {
            m_ok = false;
        }

        return true;
    }

    bool error_result(int errnum, const std::string& errmsg, const std::string& sqlstate) override
    {
        m_ok = false;
        return true;
    }

    bool ok() const
    {
        return m_ok;
    }

private:
    // Set to true if the query contains an error
    bool m_ok = true;
};

// The concrete implementation class is defined in the source file. We can't include the ODBC files in a
// header as they add very disruptive defines that cause problems elsewhere.
class ODBCImp;

class ODBC
{
public:
    ODBC(const ODBC&) = delete;
    ODBC& operator=(const ODBC&) = delete;

    ODBC(ODBC&&);
    ODBC& operator=(ODBC&&);

    // Some constants for the MariaDB ODBC driver, needed for the OPTION value
    enum MariaDBOpts : uint64_t
    {
        NO_CACHE    = 1048576,
        FORWARDONLY = 2097152,
        MULTI_STMT  = 67108864
    };

    /**
     * Get available ODBC drivers
     *
     * @return Map of driver definitions by name and their parameters. Only returns drivers for which a driver
     *         library was found.
     */
    static std::map<std::string, std::map<std::string, std::string>> drivers();

    /**
     * Create a new ODBC instance
     *
     * @param dsn     The connection string given to the driver manager
     * @param timeout Connection and query timeout
     */
    ODBC(std::string dsn, std::chrono::seconds timeout = 30s);

    ~ODBC();

    /**
     * Connect to the database
     *
     * @return True if the connection was created successfully.
     */
    bool connect();

    /**
     * Disconnect the connection
     */
    void disconnect();

    /**
     * Get the latest error message
     *
     * @return The latest error message or an empty string if no errors have occurred
     */
    const std::string& error() const;

    /**
     * Get the latest error number
     *
     * @return The latest error number or 0 if no errors have occurred
     */
    int errnum() const;

    /**
     * Get the latest SQLSTATE
     *
     * @return The latest SQLSTATE or an empty string if no SQLSTATE is available
     */
    const std::string& sqlstate() const;

    // By default the output is ignored
    static Output* ignore_result()
    {
        static NoResult no_result;
        return &no_result;
    }

    /**
     * Execute a query
     *
     * @param sql SQL to execute
     * @param output The output formatter class. By default the output is discarded.
     *
     * @return True if the query was successfully executed and at least one non-error result was returned.
     *         Partially successful results (e.g. multi-statement SQL) can be detected by inspecting whether
     *         errnum() is set.
     */
    bool query(const std::string& sql, Output* output = ignore_result());

    /**
     * Prepare a query
     *
     * @param sql SQL to prepare
     *
     * @return True if the preparation was successfully
     */
    bool prepare(const std::string& sql);

    /**
     * Unprepare a query
     *
     * This releases a previously prepared statement.
     *
     * @return True if the prepared statement was released successfully
     */
    bool unprepare();

    /**
     * Commit a transaction
     *
     * @return True if the transaction was committed successfully
     */
    bool commit();

    /**
     * Rollback a transaction
     *
     * @return True if the transaction was rolled back successfully
     */
    bool rollback();

    /**
     * Return the number of columns returned by a prepared statement
     *
     * @return The number of columns or -1 if the information is not available
     */
    int num_columns();

    /**
     * Return the number of input parameters in a prepared statement
     *
     * @return The number of parameters or -1 if the information is not available
     */
    int num_params();

    /**
     * Execute a prepared query
     *
     * @param output The output formatter class. By default the output is discarded.
     *
     * @return True if the query execution was successfully executed and at least one non-error result was
     *         returned. Partially successful results (e.g. multi-statement SQL) can be detected by inspecting
     *         whether errnum() is set.
     */
    bool execute(Output* output = ignore_result());

    /**
     * Streams the results of another connection into this one
     *
     * The SQL that defines the output stream (usually an INSERT statement) must be prepared before this
     * function is called.
     *
     * @return The Output class pointer that is given to query()
     */
    Output* as_output();

    /**
     * Set maximum number of rows to fetch
     *
     * @param limit The number of rows to fetch, 0 for no limit.
     */
    void set_row_limit(size_t limit);

    /**
     * Set query timeout
     *
     * @param timeout The timeout in seconds
     */
    void set_query_timeout(std::chrono::seconds timeout);

    /**
     * Get query timeout
     *
     * @return The timeout in seconds
     */
    std::chrono::seconds query_timeout() const;

    /**
     * Get the name of the driver being used
     *
     * @return The name of the driver if it could be retrieved or an empty string if the connection is not
     *         open or an error occurred while fetching the information.
     */
    std::string driver_name() const;

    /**
     * Get the version of the driver being used
     *
     * @return The version of the driver if it could be retrieved or an empty string if the connection is not
     *         open or an error occurred while fetching the information.
     */
    std::string driver_version() const;

    /**
     * Attempts to cancel the currently active query.
     */
    void cancel();

    //
    // ODBC catalog functions
    //

    // SQLColumns: https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlcolumns-function
    std::optional<TextResult::Result>
    columns(std::string_view catalog, std::string_view schema, std::string_view table);

    // SQLStatistics: https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlstatistics-function
    std::optional<TextResult::Result>
    statistics(std::string_view catalog, std::string_view schema, std::string_view table);

    // SQLPrimaryKeys: https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlprimarykeys-function
    std::optional<TextResult::Result>
    primary_keys(std::string_view catalog, std::string_view schema, std::string_view table);

    // SQLForeignKeys: https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlforeignkeys-function
    std::optional<TextResult::Result>
    foreign_keys(std::string_view catalog, std::string_view schema, std::string_view table);

private:
    std::unique_ptr<ODBCImp> m_imp;
};

/**
 * Enable/disable the logging of all SQL statements MaxScale sends to
 * the servers via ODBC connections.
 *
 * @param enable Whether to log the SQL statements as they are executed or prepared
 */
void odbc_set_log_statements(bool enable);
}
