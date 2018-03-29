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
 * Class which encapsulates a gtid triplet (one <domain>-<server>-<sequence>)
 */
class GtidTriplet
{
public:
    uint32_t domain;
    int64_t server_id; // Is actually 32bit unsigned. 0 is only used by server versions  <= 10.1
    uint64_t sequence;

    GtidTriplet();
    GtidTriplet(uint32_t _domain, int64_t _server_id, uint64_t _sequence);

    /**
     * Parse a Gtid-triplet from a string. In case of a multi-triplet value, only the triplet with
     * the given domain is returned. TODO: Remove once no longer used
     *
     * @param str Gtid string
     * @param search_domain The Gtid domain whose triplet should be returned. Negative domain stands for
     * autoselect, which is only allowed when the string contains one triplet.
     */
    GtidTriplet(const char* str, int64_t search_domain = -1);

    /**
     * Parse one triplet from null-terminated string. Handles multi-domain gtid:s properly. Should be called
     * repeatedly for a multi-domain gtid string by giving the value of @c endptr as @c str.
     *
     * @param str First number of a triplet in a gtid-string
     * @param endptr A pointer to save the position at after the last parsed character.
     * @return A new GtidTriplet. If an error occurs, the server_id of the returned triplet is -1.
     */
    static GtidTriplet parse_one_triplet(const char* str, char** endptr);

    bool eq(const GtidTriplet& rhs) const;

    std::string to_string() const;

    /**
     * Comparator, used when sorting by domain id.
     *
     * @param triplet1 Left side
     * @param triplet2 Right side
     * @return True if left < right
     */
    static bool compare_domains(const GtidTriplet& triplet1, const GtidTriplet& triplet2)
    {
        return triplet1.domain < triplet2.domain;
    }

private:
    void parse_triplet(const char* str);
};

inline bool operator == (const GtidTriplet& lhs, const GtidTriplet& rhs)
{
        return lhs.eq(rhs);
}

class Gtid
{
public:
    enum substraction_mode_t
    {
        MISSING_DOMAIN_IGNORE,
        MISSING_DOMAIN_LHS_ADD
    };

    /**
     * Parse the gtid string and return an object. Orders the triplets by domain id.
     *
     * @param gtid_string gtid as given by server. String must not be empty.
     * @return The parsed (possibly multidomain) gtid. In case of error, the gtid will be empty.
     */
    static Gtid from_string(const std::string& gtid_string);

    /**
     * Return a string version of the gtid.
     *
     * @return A string similar in form to how the server displays it
     */
    std::string to_string() const;

    /**
     * Check if a server with this gtid can replicate from a master with a given gtid. Only considers
     * gtid:s and only detects obvious errors. The non-detected errors will mostly be detected once
     * the slave tries to start replicating.
     *
     * TODO: Add support for Replicate_Do/Ignore_Id:s
     *
     * @param master_gtid Master server gtid
     * @return True if replication looks possible
     */
    bool can_replicate_from(const Gtid& master_gtid);

    /**
     * Is the gtid empty.
     *
     * @return True if gtid has 0 triplets
     */
    bool empty() const;

    /**
     * Full comparison.
     *
     * @param rhs Other gtid
     * @return True if both gtid:s have identical triplets or both are empty
     */
    bool operator == (const Gtid& rhs) const;

    /**
     * Calculate the number of events between two gtid:s with possibly multiple triplets. The
     * result is always 0 or greater: if a sequence number of a domain on rhs is greater than on the same
     * domain on lhs, the sequences are considered identical. Missing domains are handled depending on the
     * value of @c domain_substraction_mode.
     *
     * @param lhs The value substracted from
     * @param io_pos The value doing the substracting
     * @param domain_substraction_mode How domains that exist on one side but not the other are handled. If
     * MISSING_DOMAIN_IGNORE, these are simply ignored. If MISSING_DOMAIN_LHS_ADD, the sequence number on lhs
     * is added to the total difference.
     * @return The number of events between the two gtid:s
     */
    static uint64_t events_ahead(const Gtid& lhs, const Gtid& rhs,
                                 substraction_mode_t domain_substraction_mode);

    /**
     * Generate a MASTER_GTID_WAIT()-query to this gtid.
     *
     * @param timeout Maximum wait time in seconds
     * @return The query
     */
    std::string generate_master_gtid_wait_cmd(double timeout) const;

    GtidTriplet get_triplet(uint32_t domain) const;

private:
    std::vector<GtidTriplet> m_triplets;
};

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
     * Read a non-negative integer value from the current row and given column.
     *
     * @param column_ind Column index
     * @return Value as integer. 0 or greater indicates success, -1 is returned on error.
     */
    int64_t get_uint(int64_t column_ind) const;

    /**
     * Read a boolean value from the current row and given column.
     *
     * @param column_ind Column index
     * @return Value as boolean. Returns true if the text is either 'Y' or '1'.
     */
    bool get_bool(int64_t column_ind) const;

    /**
     * Read a gtid values from the current row and given column. If the field is empty, will return an invalid
     * gtid.
     *
     * @param column_ind Column index
     * @param gtid_domain Which gtid domain to parse
     * @return Value as a gtid.
     */
    GtidTriplet get_gtid(int64_t column_ind, int64_t gtid_domain) const;

private:
    MYSQL_RES* m_resultset; // Underlying result set, freed at dtor.
    std::tr1::unordered_map<string, int64_t> m_col_indexes; // Map of column name -> index
    int64_t m_columns;     // How many columns does the data have. Usually equal to column index map size.
    MYSQL_ROW m_rowdata;   // Data for current row
    int64_t m_current_row; // Index of current row
};
