#pragma once

/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include <maxscale/cppdefs.hh>
#include <string>
#include <vector>

#include <maxscale/monitor.h>

/** Utility macro for printing both MXS_ERROR and json error */
#define PRINT_MXS_JSON_ERROR(err_out, format, ...)\
    do {\
       MXS_ERROR(format, ##__VA_ARGS__);\
       if (err_out)\
       {\
            *err_out = mxs_json_error_append(*err_out, format, ##__VA_ARGS__);\
       }\
    } while (false)

using std::string;

typedef std::vector<string> StringVector;
typedef std::vector<MXS_MONITORED_SERVER*> ServerVector;

extern const int64_t SERVER_ID_UNKNOWN;

/**
 * Scan a server id from a string.
 *
 * @param id_string
 * @return Server id, or -1 if scanning fails
 */
int64_t scan_server_id(const char* id_string);

/**
 * Query one row of results, save strings to array. Any additional rows are ignored.
 *
 * @param database The database to query.
 * @param query The query to execute.
 * @param expected_cols How many columns the result should have.
 * @param output The output array to populate.
 * @return True on success.
 */
bool query_one_row(MXS_MONITORED_SERVER *database, const char* query, unsigned int expected_cols,
                   StringVector* output);

/**
 * Get MariaDB connection error strings from all the given servers, form one string.
 *
 * @param slaves Servers with errors
 * @return Concatenated string.
 */
string get_connection_errors(const ServerVector& servers);

/**
 * Generates a list of server names separated by ', '
 *
 * @param array The servers
 * @return Server names
 */
string monitored_servers_to_string(const ServerVector& array);

/**
 * Helper class for simplifying working with resultsets. Used in MariaDBServer.
 */
class QueryResult
{
    // These need to be banned to avoid premature destruction.
    QueryResult(const QueryResult&) = delete;
    QueryResult& operator = (const QueryResult&) = delete;

public:
    QueryResult(MYSQL_RES* resultset = NULL);
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
     * @return Current row index, or -1 if no data or next_row() has not been called yet.
     */
    int64_t get_row_index() const;

    /**
     * How many columns the result set has.
     *
     * @return Column count, or -1 if no data.
     */
    int64_t get_column_count() const;

    /**
     * Get a numeric index for a column name. May give wrong results if column names are not unique.
     *
     * @param col_name Column name
     * @return Index or -1 if not found.
     */
    int64_t get_col_index(const string& col_name) const;

    /**
     * Read a string value from the current row and given column. Empty string and (null) are both interpreted
     * as the empty string.
     *
     * @param column_ind Column index
     * @return Value as string
     */
    string get_string(int64_t column_ind) const;

    /**
     * Read an integer value from the current row and given column. No error checking is done on the parsing.
     * The parsing is performed by @c strtoll(), so the caller may check errno for errors.
     *
     * @param column_ind Column index
     * @return Value as integer
     */
    int64_t get_int(int64_t column_ind) const;

    /**
     * Read a boolean value from the current row and given column.
     *
     * @param column_ind Column index
     * @return Value as boolean. Returns true if the text is either 'Y' or '1'.
     */
    bool get_bool(int64_t column_ind) const;

private:
    MYSQL_RES* m_resultset; // Underlying result set, freed at dtor.
    std::tr1::unordered_map<string, int64_t> m_col_indexes; // Map of column name -> index
    int64_t m_columns;     // How many columns does the data have. Usually equal to column index map size.
    MYSQL_ROW m_rowdata;   // Data for current row
    int64_t m_current_row; // Index of current row
};
