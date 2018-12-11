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
#include "server_utils.hh"

#include <algorithm>
#include <inttypes.h>
#include <maxbase/format.hh>
#include <maxbase/assert.h>


using std::string;
using maxbase::string_printf;

namespace
{
// Used for Slave_IO_Running
const char YES[] = "Yes";
const char PREPARING[] = "Preparing";
const char CONNECTING[] = "Connecting";
const char NO[] = "No";
}

string SlaveStatus::to_string() const
{
    // Print all of this on the same line to make things compact. Are the widths reasonable? The format is
    // not quite array-like since usually there is just one row. May be changed later.
    // Form the components of the line.
    string host_port = string_printf("[%s]:%d", master_host.c_str(), master_port);
    string running_states = string_printf("%s/%s",
                                          slave_io_to_string(slave_io_running).c_str(),
                                          slave_sql_running ? "Yes" : "No");

    string rval = string_printf(
        "  Host: %22s, IO/SQL running: %7s, Master ID: %4" PRId64 ", Gtid_IO_Pos: %s, R.Lag: %d",
        host_port.c_str(),
        running_states.c_str(),
        master_server_id,
        gtid_io_pos.to_string().c_str(),
        seconds_behind_master);
    return rval;
}

string SlaveStatus::to_short_string() const
{
    if (name.empty())
    {
        return string_printf("Slave connection from %s to [%s]:%i",
                             owning_server.c_str(), master_host.c_str(), master_port);
    }
    else
    {
        return string_printf("Slave connection '%s' from %s to [%s]:%i",
                             name.c_str(), owning_server.c_str(), master_host.c_str(), master_port);
    }
}

json_t* SlaveStatus::to_json() const
{
    json_t* result = json_object();
    json_object_set_new(result, "connection_name", json_string(name.c_str()));
    json_object_set_new(result, "master_host", json_string(master_host.c_str()));
    json_object_set_new(result, "master_port", json_integer(master_port));
    json_object_set_new(result,
                        "slave_io_running",
                        json_string(slave_io_to_string(slave_io_running).c_str()));
    json_object_set_new(result, "slave_sql_running", json_string(slave_sql_running ? "Yes" : "No"));
    json_object_set_new(result, "seconds_behing_master",
                        seconds_behind_master == SERVER::RLAG_UNDEFINED ? json_null() :
                        json_integer(seconds_behind_master));
    json_object_set_new(result, "master_server_id", json_integer(master_server_id));
    json_object_set_new(result, "last_io_or_sql_error", json_string(last_error.c_str()));
    json_object_set_new(result, "gtid_io_pos", json_string(gtid_io_pos.to_string().c_str()));
    return result;
}
SlaveStatus::slave_io_running_t SlaveStatus::slave_io_from_string(const std::string& str)
{
    slave_io_running_t rval = SLAVE_IO_NO;
    if (str == YES)
    {
        rval = SLAVE_IO_YES;
    }
    // Interpret "Preparing" as "Connecting". It's not quite clear if the master server id has been read
    // or if server versions between master and slave have been checked, so better be on the safe side.
    else if (str == CONNECTING || str == PREPARING)
    {
        rval = SLAVE_IO_CONNECTING;
    }
    else if (str != NO)
    {
        MXS_ERROR("Unexpected value for Slave_IO_Running: '%s'.", str.c_str());
    }
    return rval;
}

string SlaveStatus::slave_io_to_string(SlaveStatus::slave_io_running_t slave_io)
{
    string rval;
    switch (slave_io)
    {
    case SlaveStatus::SLAVE_IO_YES:
        rval = YES;
        break;

    case SlaveStatus::SLAVE_IO_CONNECTING:
        rval = CONNECTING;
        break;

    case SlaveStatus::SLAVE_IO_NO:
        rval = NO;
        break;

    default:
        mxb_assert(!false);
    }
    return rval;
}

bool SlaveStatus::should_be_copied(string* ignore_reason_out) const
{
    bool accepted = true;
    auto master_id = master_server_id;
    // The connection is only copied if it is running or at least has been seen running.
    // Also, target should not be this server.
    string ignore_reason;
    if (!slave_sql_running)
    {
        accepted = false;
        ignore_reason = "its slave sql thread is not running.";
    }
    else if (!seen_connected)
    {
        accepted = false;
        ignore_reason = "it has not been seen connected to master.";
    }
    else if (master_id <= 0)
    {
        accepted = false;
        ignore_reason = string_printf("its Master_Server_Id (%" PRIi64 ") is invalid .", master_id);
    }

    if (!accepted)
    {
        *ignore_reason_out = ignore_reason;
    }
    return accepted;
}

ServerOperation::ServerOperation(MariaDBServer* target, bool was_is_master,
                                 bool handle_events, const std::string& sql_file,
                                 const SlaveStatusArray& conns_to_copy)
    : target(target)
    , to_from_master(was_is_master)
    , handle_events(handle_events)
    , sql_file(sql_file)
    , conns_to_copy(conns_to_copy)
{
}

GeneralOpData::GeneralOpData(const string& replication_user, const string& replication_password,
                             json_t** error, maxbase::Duration time_remaining)
    : replication_user(replication_user)
    , replication_password(replication_password)
    , error_out(error)
    , time_remaining(time_remaining)
{
}

GtidList GtidList::from_string(const string& gtid_string)
{
    mxb_assert(gtid_string.size());
    GtidList rval;
    bool error = false;
    bool have_more = false;
    const char* str = gtid_string.c_str();
    do
    {
        char* endptr = NULL;
        auto new_triplet = Gtid::from_string(str, &endptr);
        if (new_triplet.m_server_id == SERVER_ID_UNKNOWN)
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
    }
    while (have_more && !error);

    if (error)
    {
        // If error occurred, clear the gtid as something is very wrong.
        rval.m_triplets.clear();
    }
    else
    {
        // Usually the servers gives the triplets ordered by domain id:s, but this is not 100%.
        std::sort(rval.m_triplets.begin(), rval.m_triplets.end(), Gtid::compare_domains);
    }
    return rval;
}

string GtidList::to_string() const
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

bool GtidList::can_replicate_from(const GtidList& master_gtid)
{
    /* The result of this function is false if the source and master have a common domain id where
     * the source is ahead of the master. */
    return events_ahead(master_gtid, MISSING_DOMAIN_IGNORE) == 0;
}

bool GtidList::empty() const
{
    return m_triplets.empty();
}

bool GtidList::operator==(const GtidList& rhs) const
{
    return m_triplets == rhs.m_triplets;
}

uint64_t GtidList::events_ahead(const GtidList& rhs, substraction_mode_t domain_substraction_mode) const
{
    const size_t n_lhs = m_triplets.size();
    const size_t n_rhs = rhs.m_triplets.size();
    size_t ind_lhs = 0, ind_rhs = 0;
    uint64_t events = 0;

    // GtidLists are assumed to be ordered by domain in ascending order.
    while (ind_lhs < n_lhs && ind_rhs < n_rhs)
    {
        auto lhs_triplet = m_triplets[ind_lhs];
        auto rhs_triplet = rhs.m_triplets[ind_rhs];
        // Server id -1 should never be saved in a real gtid variable.
        mxb_assert(lhs_triplet.m_server_id != SERVER_ID_UNKNOWN
                   && rhs_triplet.m_server_id != SERVER_ID_UNKNOWN);
        // Search for matching domain_id:s, advance the smaller one.
        if (lhs_triplet.m_domain < rhs_triplet.m_domain)
        {
            if (domain_substraction_mode == MISSING_DOMAIN_LHS_ADD)
            {
                // The domain on lhs does not exist on rhs. Add entire sequence number of lhs to the result.
                events += lhs_triplet.m_sequence;
            }
            ind_lhs++;
        }
        else if (lhs_triplet.m_domain > rhs_triplet.m_domain)
        {
            ind_rhs++;
        }
        else
        {
            // Domains match, check sequences.
            if (lhs_triplet.m_sequence > rhs_triplet.m_sequence)
            {
                /* Same domains, but lhs sequence is ahead of rhs sequence.  */
                events += lhs_triplet.m_sequence - rhs_triplet.m_sequence;
            }
            // Continue to next domains.
            ind_lhs++;
            ind_rhs++;
        }
    }

    // If LHS has domains with higher id:s than at RHS, those domains need to be iterated here.
    // This only affects the result if the LHS_ADD-mode is used.
    if (domain_substraction_mode == MISSING_DOMAIN_LHS_ADD)
    {
        for (; ind_lhs < n_lhs; ind_lhs++)
        {
            events += m_triplets[ind_lhs].m_sequence;
        }
    }
    return events;
}

Gtid Gtid::from_string(const char* str, char** endptr)
{
    /* Error checking the gtid string is a bit questionable, as having an error means that the server is
     *  buggy or network has faults, in which case nothing can be trusted. But without error checking
     *  MaxScale may crash if string is wrong. */
    mxb_assert(endptr);
    const char* ptr = str;
    char* strtoull_endptr = NULL;
    // Parse three numbers separated by -
    uint64_t parsed_numbers[3];
    bool error = false;
    for (int i = 0; i < 3 && !error; i++)
    {
        errno = 0;
        parsed_numbers[i] = strtoull(ptr, &strtoull_endptr, 10);
        // Check for parse error. Even this is not quite enough because strtoull will silently convert
        // negative values. Yet, strtoull is required for the third value.
        if (errno != 0 || strtoull_endptr == ptr)
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
        return Gtid((uint32_t)parsed_numbers[0], parsed_numbers[1], parsed_numbers[2]);
    }
    else
    {
        return Gtid();
    }
}

Gtid::Gtid()
    : m_domain(0)
    , m_server_id(SERVER_ID_UNKNOWN)
    , m_sequence(0)
{
}

Gtid::Gtid(uint32_t domain, int64_t server_id, uint64_t sequence)
    : m_domain(domain)
    , m_server_id(server_id)
    , m_sequence(sequence)
{
}

bool Gtid::eq(const Gtid& rhs) const
{
    return m_domain == rhs.m_domain && m_server_id == rhs.m_server_id && m_sequence == rhs.m_sequence;
}

string Gtid::to_string() const
{
    string rval;
    if (m_server_id != SERVER_ID_UNKNOWN)
    {
        rval += string_printf("%" PRIu32 "-%" PRIi64 "-%" PRIu64, m_domain, m_server_id, m_sequence);
    }
    return rval;
}

Gtid GtidList::get_gtid(uint32_t domain) const
{
    Gtid rval;
    // Make a dummy triplet for the domain search
    Gtid search_val(domain, -1, 0);
    auto found = std::lower_bound(m_triplets.begin(),
                                  m_triplets.end(),
                                  search_val,
                                  Gtid::compare_domains);
    if (found != m_triplets.end() && found->m_domain == domain)
    {
        rval = *found;
    }
    return rval;
}

QueryResult::QueryResult(MYSQL_RES* resultset)
    : m_resultset(resultset)
{
    if (m_resultset)
    {
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
    mxb_assert(m_resultset);
    m_rowdata = mysql_fetch_row(m_resultset);
    if (m_rowdata)
    {
        m_current_row_ind++;
        return true;
    }
    return false;
}

int64_t QueryResult::get_current_row_index() const
{
    return m_current_row_ind;
}

int64_t QueryResult::get_col_count() const
{
    return m_resultset ? mysql_num_fields(m_resultset) : -1;
}

int64_t QueryResult::get_row_count() const
{
    return m_resultset ? mysql_num_rows(m_resultset) : -1;
}

int64_t QueryResult::get_col_index(const string& col_name) const
{
    auto iter = m_col_indexes.find(col_name);
    return (iter != m_col_indexes.end()) ? iter->second : -1;
}

string QueryResult::get_string(int64_t column_ind) const
{
    mxb_assert(column_ind < get_col_count() && column_ind >= 0);
    char* data = m_rowdata[column_ind];
    return data ? data : "";
}

int64_t QueryResult::get_uint(int64_t column_ind) const
{
    mxb_assert(column_ind < get_col_count() && column_ind >= 0);
    char* data = m_rowdata[column_ind];
    int64_t rval = -1;
    if (data && *data)
    {
        errno = 0;      // strtoll sets this
        char* endptr = NULL;
        auto parsed = strtoll(data, &endptr, 10);
        if (parsed >= 0 && errno == 0 && *endptr == '\0')
        {
            rval = parsed;
        }
    }
    return rval;
}

bool QueryResult::get_bool(int64_t column_ind) const
{
    mxb_assert(column_ind < get_col_count() && column_ind >= 0);
    char* data = m_rowdata[column_ind];
    return data ? (strcmp(data, "Y") == 0 || strcmp(data, "1") == 0) : false;
}
