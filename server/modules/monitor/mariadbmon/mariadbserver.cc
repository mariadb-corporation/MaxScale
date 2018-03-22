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

#include "mariadbserver.hh"

#include <inttypes.h>
#include <sstream>
#include <maxscale/mysql_utils.h>
#include "utilities.hh"

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

string Gtid::generate_master_gtid_wait_cmd(double timeout) const
{
    std::stringstream query_ss;
    query_ss << "SELECT MASTER_GTID_WAIT(\"" << to_string() << "\", " << timeout << ");";
    return query_ss.str();
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

std::auto_ptr<QueryResult> MariaDBServer::execute_query(const string& query)
{
    auto conn = server_base->con;
    std::auto_ptr<QueryResult> rval;
    MYSQL_RES *result = NULL;
    if (mxs_mysql_query(conn, query.c_str()) == 0 && (result = mysql_store_result(conn)) != NULL)
    {
        rval = std::auto_ptr<QueryResult>(new QueryResult(result));
    }
    else
    {
        mon_report_query_error(server_base);
    }
    return rval;
}