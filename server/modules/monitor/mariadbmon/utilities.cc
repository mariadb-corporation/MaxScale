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

Gtid::Gtid()
    : domain(0)
    , server_id(SERVER_ID_UNKNOWN)
    , sequence(0)
{}

Gtid::Gtid(const char* str, int64_t search_domain)
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

bool Gtid::operator == (const Gtid& rhs) const
{
    return domain == rhs.domain &&
        server_id != SERVER_ID_UNKNOWN && server_id == rhs.server_id &&
        sequence == rhs.sequence;
}

string Gtid::to_string() const
{
    std::stringstream ss;
    if (server_id != SERVER_ID_UNKNOWN)
    {
        ss << domain << "-" << server_id << "-" << sequence;
    }
    return ss.str();
}

void Gtid::parse_triplet(const char* str)
{
    ss_debug(int rv = ) sscanf(str, "%" PRIu32 "-%" PRId64 "-%" PRIu64, &domain, &server_id, &sequence);
    ss_dassert(rv == 3);
}

SlaveStatusInfo::SlaveStatusInfo()
    : master_server_id(SERVER_ID_UNKNOWN)
    , master_port(0)
    , slave_io_running(false)
    , slave_sql_running(false)
    , read_master_log_pos(0)
{}

MariaDBServer::MariaDBServer(MXS_MONITORED_SERVER* monitored_server)
    : server_base(monitored_server)
    , version(MYSQL_SERVER_VERSION_51)
    , server_id(SERVER_ID_UNKNOWN)
    , group(0)
    , read_only(false)
    , slave_configured(false)
    , binlog_relay(false)
    , n_slaves_configured(0)
    , n_slaves_running(0)
    , slave_heartbeats(0)
    , heartbeat_period(0)
    , latest_event(0)
    , gtid_domain_id(-1)
{}

int64_t MariaDBServer::relay_log_events()
{
    if (slave_status.gtid_io_pos.server_id != SERVER_ID_UNKNOWN &&
        gtid_current_pos.server_id != SERVER_ID_UNKNOWN &&
        slave_status.gtid_io_pos.domain == gtid_current_pos.domain &&
        slave_status.gtid_io_pos.sequence >= gtid_current_pos.sequence)
    {
        return slave_status.gtid_io_pos.sequence - gtid_current_pos.sequence;
    }
    return -1;
}

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

string generate_master_gtid_wait_cmd(const Gtid& gtid, double timeout)
{
    std::stringstream query_ss;
    query_ss << "SELECT MASTER_GTID_WAIT(\"" << gtid.to_string() << "\", " << timeout << ");";
    return query_ss.str();
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
