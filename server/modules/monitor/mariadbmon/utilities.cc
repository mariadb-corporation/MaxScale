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

#include "utilities.hh"

#include <algorithm>
#include <inttypes.h>
#include <limits>
#include <stdio.h>
#include <string>
#include <sstream>
#include <maxscale/dcb.h>
#include <maxscale/debug.h>
#include <maxscale/mysql_utils.h>

/** Server id default value */
const int64_t SERVER_ID_UNKNOWN = -1;

int64_t scan_server_id(const char* id_string)
{
    int64_t server_id = SERVER_ID_UNKNOWN;
    ss_debug(int rv = ) sscanf(id_string, "%" PRId64, &server_id);
    ss_dassert(rv == 1);
    // Server id can be 0, which was even the default value until 10.2.1.
    // KB is a bit hazy on this, but apparently when replicating, the server id should not be 0. Not sure,
    // so MaxScale allows this.
#if defined(SS_DEBUG)
    const int64_t SERVER_ID_MIN = std::numeric_limits<uint32_t>::min();
    const int64_t SERVER_ID_MAX = std::numeric_limits<uint32_t>::max();
#endif
    ss_dassert(server_id >= SERVER_ID_MIN && server_id <= SERVER_ID_MAX);
    return server_id;
}

bool query_one_row(MXS_MONITORED_SERVER *database, const char* query, unsigned int expected_cols,
                   StringVector* output)
{
    bool rval = false;
    MYSQL_RES *result;
    if (mxs_mysql_query(database->con, query) == 0 && (result = mysql_store_result(database->con)) != NULL)
    {
        unsigned int columns = mysql_field_count(database->con);
        if (columns != expected_cols)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for '%s'. Expected %d columns, got %d. Server version: %s",
                      query, expected_cols, columns, database->server->version_string);
        }
        else
        {
            MYSQL_ROW row = mysql_fetch_row(result);
            if (row)
            {
                for (unsigned int i = 0; i < columns; i++)
                {
                    output->push_back((row[i] != NULL) ? row[i] : "");
                }
                rval = true;
            }
            else
            {
                MXS_ERROR("Query '%s' returned no rows.", query);
            }
            mysql_free_result(result);
        }
    }
    else
    {
        mon_report_query_error(database);
    }
    return rval;
}

string get_connection_errors(const ServerVector& servers)
{
    // Get errors from all connections, form a string.
    std::stringstream ss;
    for (ServerVector::const_iterator iter = servers.begin(); iter != servers.end(); iter++)
    {
        const char* error = mysql_error((*iter)->con);
        ss_dassert(*error); // Every connection should have an error.
        ss << (*iter)->server->unique_name << ": '" << error << "'";
        if (iter + 1 != servers.end())
        {
            ss << ", ";
        }
    }
    return ss.str();
}

string monitored_servers_to_string(const ServerVector& array)
{
    string rval;
    size_t array_size = array.size();
    if (array_size > 0)
    {
        const char* separator = "";
        for (size_t i = 0; i < array_size; i++)
        {
            rval += separator;
            rval += array[i]->server->unique_name;
            separator = ",";
        }
    }
    return rval;
}

QueryResult::QueryResult(MYSQL_RES* resultset)
    : m_resultset(resultset)
    , m_columns(-1)
    , m_rowdata(NULL)
    , m_current_row(-1)
{
    if (m_resultset)
    {
        m_columns = mysql_num_fields(m_resultset);
        MYSQL_FIELD* field_info = mysql_fetch_fields(m_resultset);
        for (int64_t column_index = 0; column_index < m_columns; column_index++)
        {
            string key(field_info[column_index].name);
            // TODO: Think of a way to handle duplicate names nicely. Currently this should only be used
            // for known queries.
            ss_dassert(m_col_indexes.count(key) == 0);
            m_col_indexes[key] = column_index;
        }
    }
}

QueryResult::~QueryResult()
{
    if (m_resultset)
    {
        mysql_free_result(m_resultset);
    }
}

bool QueryResult::next_row()
{
    m_rowdata = mysql_fetch_row(m_resultset);
    if (m_rowdata != NULL)
    {
        m_current_row++;
        return true;
    }
    return false;
}

int64_t QueryResult::get_row_index() const
{
    return m_current_row;
}

int64_t QueryResult::get_column_count() const
{
    return m_columns;
}

int64_t QueryResult::get_col_index(const string& col_name) const
{
    auto iter = m_col_indexes.find(col_name);
    return (iter != m_col_indexes.end()) ? iter->second : -1;
}

string QueryResult::get_string(int64_t column_ind) const
{
    ss_dassert(column_ind < m_columns);
    char* data = m_rowdata[column_ind];
    return data ? data : "";
}

int64_t QueryResult::get_uint(int64_t column_ind) const
{
    ss_dassert(column_ind < m_columns);
    char* data = m_rowdata[column_ind];
    int64_t rval = -1;
    if (data)
    {
        errno = 0; // strtoll sets this
        auto parsed = strtoll(data, NULL, 10);
        if (parsed >= 0 && errno == 0)
        {
            rval = parsed;
        }
    }
    return rval;
}

bool QueryResult::get_bool(int64_t column_ind) const
{
    ss_dassert(column_ind < m_columns);
    char* data = m_rowdata[column_ind];
    return data ? (strcmp(data,"Y") == 0 || strcmp(data, "1") == 0) : false;
}

GtidTriplet QueryResult::get_gtid(int64_t column_ind, int64_t gtid_domain) const
{
    ss_dassert(column_ind < m_columns);
    char* data = m_rowdata[column_ind];
    GtidTriplet rval;
    if (data && *data)
    {
        rval = GtidTriplet(data, gtid_domain);
    }
    return rval;
}

Gtid Gtid::from_string(const std::string& gtid_string)
{
    ss_dassert(gtid_string.size());
    Gtid rval;
    bool error = false;
    bool have_more = false;
    const char* str = gtid_string.c_str();
    do
    {
        char* endptr = NULL;
        auto new_triplet = GtidTriplet::parse_one_triplet(str, &endptr);
        if (new_triplet.server_id == SERVER_ID_UNKNOWN)
        {
            error = true;
        }
        else
        {
            rval.m_triplets.push_back(new_triplet);
            // The last number must be followed by ',' (another triplet) or \0 (last triplet)
            if (*endptr == ',')
            {
                have_more = true;
                str = endptr + 1;
            }
            else if (*endptr == '\0')
            {
                have_more = false;
            }
            else
            {
                error = true;
            }
        }
    } while (have_more && !error);

    if (error)
    {
        // If error occurred, clear the gtid as something is very wrong.
        rval.m_triplets.clear();
    }
    else
    {
        // Usually the servers gives the triplets ordered by domain id:s, but this is not 100%.
        std::sort(rval.m_triplets.begin(), rval.m_triplets.end(), GtidTriplet::compare_domains);
    }
    return rval;
}

string Gtid::to_string() const
{
    string rval;
    string separator;
    for (auto iter = m_triplets.begin(); iter != m_triplets.end(); iter++)
    {
        rval += separator + iter->to_string();
        separator = ",";
    }
    return rval;
}

bool Gtid::can_replicate_from(const Gtid& master_gtid)
{
    /* The result of this function is false if the source and master have a common domain id where
     * the source is ahead of the master. */
    return (events_ahead(*this, master_gtid, MISSING_DOMAIN_IGNORE) == 0);
}

bool Gtid::empty() const
{
    return m_triplets.empty();
}

bool Gtid::operator == (const Gtid& rhs) const
{
    return m_triplets == rhs.m_triplets;
}

uint64_t Gtid::events_ahead(const Gtid& lhs, const Gtid& rhs, substraction_mode_t domain_substraction_mode)
{
    const size_t n_lhs = lhs.m_triplets.size();
    const size_t n_rhs = rhs.m_triplets.size();
    size_t ind_lhs = 0, ind_rhs = 0;
    uint64_t events = 0;

    while (ind_lhs < n_lhs && ind_rhs < n_rhs)
    {
        auto lhs_triplet = lhs.m_triplets[ind_lhs];
        auto rhs_triplet = rhs.m_triplets[ind_rhs];
        // Server id -1 should never be saved in a real gtid variable.
        ss_dassert(lhs_triplet.server_id != SERVER_ID_UNKNOWN &&
                   rhs_triplet.server_id != SERVER_ID_UNKNOWN);
        // Search for matching domain_id:s, advance the smaller one.
        if (lhs_triplet.domain < rhs_triplet.domain)
        {
            if (domain_substraction_mode == MISSING_DOMAIN_LHS_ADD)
            {
                // The domain on lhs does not exist on rhs. Add entire sequence number of lhs to the result.
                events += lhs_triplet.sequence;
            }
            ind_lhs++;
        }
        else if (lhs_triplet.domain > rhs_triplet.domain)
        {
            ind_rhs++;
        }
        else
        {
            // Domains match, check sequences.
            if (lhs_triplet.sequence > rhs_triplet.sequence)
            {
                /* Same domains, but lhs sequence is equal or ahead of rhs sequence.  */
                events += lhs_triplet.sequence - rhs_triplet.sequence;
            }
            // Continue to next domains.
            ind_lhs++;
            ind_rhs++;
        }
    }
    return events;
}

GtidTriplet GtidTriplet::parse_one_triplet(const char* str, char** endptr)
{
    /* Error checking the gtid string is a bit questionable, as having an error means that the server is
       buggy or network has faults, in which case nothing can be trusted. But without error checking
       MaxScale may crash if string is wrong. */
    ss_dassert(endptr);
    const char* ptr = str;
    char* strtoull_endptr = NULL;
    // Parse three numbers separated by -
    uint64_t parsed_numbers[3];
    bool error = false;
    for (int i = 0; i < 3 && !error; i++)
    {
        errno = 0;
        parsed_numbers[i] = strtoull(ptr, &strtoull_endptr, 10);
        // No parse error
        if (errno != 0)
        {
            error = true;
        }
        else if (i < 2)
        {
            // First two numbers must be followed by a -
            if (*strtoull_endptr == '-')
            {
                ptr = strtoull_endptr + 1;
            }
            else
            {
                error = true;
            }
        }
    }

    // Check that none of the parsed numbers are unexpectedly large. This shouldn't really be possible unless
    // server has a bug or network had an error.
    if (!error && (parsed_numbers[0] > UINT32_MAX || parsed_numbers[1] > UINT32_MAX))
    {
        error = true;
    }

    if (!error)
    {
        *endptr = strtoull_endptr;
        return GtidTriplet((uint32_t)parsed_numbers[0], parsed_numbers[1], parsed_numbers[2]);
    }
    else
    {
        return GtidTriplet();
    }
}

GtidTriplet::GtidTriplet()
    : domain(0)
    , server_id(SERVER_ID_UNKNOWN)
    , sequence(0)
{}

GtidTriplet::GtidTriplet(uint32_t _domain, int64_t _server_id, uint64_t _sequence)
    : domain(_domain)
    , server_id(_server_id)
    , sequence(_sequence)
{}

GtidTriplet::GtidTriplet(const char* str, int64_t search_domain)
    : domain(0)
    , server_id(SERVER_ID_UNKNOWN)
    , sequence(0)
{
    // Autoselect only allowed with one triplet
    ss_dassert(search_domain >= 0 || strchr(str, ',') == NULL);
    parse_triplet(str);
    if (search_domain >= 0 && domain != search_domain)
    {
        // Search for the correct triplet.
        bool found = false;
        for (const char* next_triplet = strchr(str, ',');
             next_triplet != NULL && !found;
             next_triplet = strchr(next_triplet, ','))
        {
            parse_triplet(++next_triplet);
            if (domain == search_domain)
            {
                found = true;
            }
        }
        ss_dassert(found);
    }
}

bool GtidTriplet::eq(const GtidTriplet& rhs) const
{
    return domain == rhs.domain && server_id == rhs.server_id && sequence == rhs.sequence;
}

string GtidTriplet::to_string() const
{
    std::stringstream ss;
    if (server_id != SERVER_ID_UNKNOWN)
    {
        ss << domain << "-" << server_id << "-" << sequence;
    }
    return ss.str();
}

void GtidTriplet::parse_triplet(const char* str)
{
    ss_debug(int rv = ) sscanf(str, "%" PRIu32 "-%" PRId64 "-%" PRIu64, &domain, &server_id, &sequence);
    ss_dassert(rv == 3);
}

string Gtid::generate_master_gtid_wait_cmd(double timeout) const
{
    std::stringstream query_ss;
    query_ss << "SELECT MASTER_GTID_WAIT(\"" << to_string() << "\", " << timeout << ");";
    return query_ss.str();
}

GtidTriplet Gtid::get_triplet(uint32_t domain) const
{
    GtidTriplet rval;
    // Make a dummy triplet for the domain search
    GtidTriplet search_val(domain, -1, 0);
    auto found = std::lower_bound(m_triplets.begin(), m_triplets.end(), search_val,
                                      GtidTriplet::compare_domains);
    if (found != m_triplets.end() && found->domain == domain)
    {
        rval = *found;
    }
    return rval;
}