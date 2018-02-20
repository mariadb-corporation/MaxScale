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
#include <stdio.h>
#include <string>
#include <sstream>
#include <maxscale/debug.h>

#include "mariadbmon.hh"

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

MySqlServerInfo::MySqlServerInfo()
    : server_id(SERVER_ID_UNKNOWN)
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
    , version(MYSQL_SERVER_VERSION_51)
{}

int64_t MySqlServerInfo::relay_log_events()
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
