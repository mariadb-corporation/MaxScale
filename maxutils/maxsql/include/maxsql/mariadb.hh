/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxsql/ccdefs.hh>
#include <string>
#include <unordered_map>
#include <mysql.h>

namespace maxsql
{
/**
 * Execute a query, manually defining retry limits.
 *
 * @param conn MySQL connection
 * @param query Query to execute
 * @param query_retries Maximum number of retries
 * @param query_retry_timeout Maximum time to spend retrying, in seconds
 * @return return value of mysql_query
 */
int mysql_query_ex(MYSQL* conn, const std::string& query, int query_retries, time_t query_retry_timeout);

/**
 * Check if the MYSQL error number is a connection error.
 *
 * @param Error code
 * @return True if the MYSQL error number is a connection error
 */
bool mysql_is_net_error(unsigned int errcode);

/**
 * Enable/disable the logging of all SQL statements MaxScale sends to
 * the servers.
 *
 * @param enable If true, enable, if false, disable.
 */
void mysql_set_log_statements(bool enable);

/**
 * Returns whether SQL statements sent to the servers are logged or not.
 *
 * @return True, if statements are logged, false otherwise.
 */
bool mysql_get_log_statements();

/**
 * Helper class for simplifying working with resultsets.
 */
class QueryResult
{
    QueryResult(const QueryResult&) = delete;
    QueryResult& operator=(const QueryResult&) = delete;

public:
    /**
     * Construct a new resultset.
     *
     * @param resultset The results from mysql_query(). Must not be NULL.
     */
    QueryResult(MYSQL_RES* resultset);

    ~QueryResult();

    /**
     * Advance to next row. Affects all result returning functions.
     *
     * @return True if the next row has data, false if the current row was the last one.
     */
    bool next_row();

    /**
     * Get the index of the current row.
     *
     * @return Current row index, or -1 if next_row() has not been called yet or all rows have been processed.
     */
    int64_t get_current_row_index() const;

    /**
     * How many columns the result set has.
     *
     * @return Column count
     */
    int64_t get_col_count() const;

    /**
     * How many rows does the result set have?
     *
     * @return The number of rows
     */
    int64_t get_row_count() const;

    /**
     * Get a numeric index for a column name. May give wrong results if column names are not unique.
     *
     * @param col_name Column name
     * @return Index or -1 if not found
     */
    int64_t get_col_index(const std::string& col_name) const;

    /**
     * Read a string value from the current row and given column. Empty string and (null) are both interpreted
     * as the empty string.
     *
     * @param column_ind Column index
     * @return Value as string
     */
    std::string get_string(int64_t column_ind) const;

    /**
     * Read an integer value from the current row and given column.
     *
     * @param column_ind Column index
     * @return Value as integer. If the data could not be parsed an error flag is set.
     */
    int64_t get_int(int64_t column_ind) const;

    /**
     * Check if field is null.
     *
     * @param column_ind Column index
     * @return True if null
     */
    bool field_is_null(int64_t column_ind) const;

    /**
     * Read a boolean-like value from the current row and given column. The function expects the field to
     * be either 1 or 0, any other value is an error.
     *
     * @param column_ind Column index
     * @return Value as boolean. Returns true if the field contains '1'.
     */
    bool get_bool(int64_t column_ind) const;

    /**
     * Has a parsing error occurred during current row?
     *
     * @return True if parsing failed.
     */
    bool error() const;

    /**
     * Return error string.
     *
     * @return Error string
     */
    std::string error_string() const;

private:

    class ConversionError
    {
    public:

        /**
         * Is error set?
         *
         * @return True if set
         */
        bool error() const;

        /**
         * Set an error describing an invalid conversion. The error is only set if the error is
         * currently empty.
         *
         * @param field_value The value of the accessed field
         * @param target_type Conversion target datatype
         */
        void set_value_error(const std::string& field_value, const std::string& target_type);

        /**
         * Set an error describing a conversion from a null value. The error is only set if the error is
         * currently empty.
         *
         * @param target_type Conversion target datatype
         */
        void set_null_value_error(const std::string& target_type);

        /**
         * Print error information to string.
         *
         * @return Error description, or empty if no error
         */
        std::string to_string() const;

    private:
        bool        m_field_was_null = false;   /**< Was the converted field null? */
        std::string m_field_value;              /**< The value in the field if it was not null */
        std::string m_target_type;              /**< The conversion target type */
    };

    int64_t parse_integer(int64_t column_ind, const std::string& target_type) const;
    void    set_error(int64_t column_ind, const std::string& target_type) const;

    MYSQL_RES* m_resultset = nullptr;   /**< Underlying result set, freed at dtor */
    MYSQL_ROW  m_rowdata = nullptr;     /**< Data for current row */
    int64_t    m_current_row_ind = -1;  /**< Index of current row */

    mutable ConversionError                  m_error;       /**< Error information */
    std::unordered_map<std::string, int64_t> m_col_indexes; /**< Map of column name -> index */
};
}
