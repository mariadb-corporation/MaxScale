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

bool MariaDBServer::do_show_slave_status(int64_t gtid_domain)
{
    /** Column positions for SHOW SLAVE STATUS */
    const size_t MYSQL55_STATUS_MASTER_LOG_POS = 5;
    const size_t MYSQL55_STATUS_MASTER_LOG_FILE = 6;
    const size_t MYSQL55_STATUS_IO_RUNNING = 10;
    const size_t MYSQL55_STATUS_SQL_RUNNING = 11;
    const size_t MYSQL55_STATUS_MASTER_ID = 39;

    bool rval = true;
    unsigned int columns;
    int i_slave_io_running, i_slave_sql_running, i_read_master_log_pos, i_master_server_id, i_master_log_file;
    int i_last_io_errno, i_last_io_error, i_last_sql_error, i_slave_rec_hbs, i_slave_hb_period;
    int i_master_host, i_master_port, i_using_gtid, i_gtid_io_pos;
    const char *query;
    mysql_server_version server_version = version;

    if (server_version == MYSQL_SERVER_VERSION_100)
    {
        columns = 42;
        query = "SHOW ALL SLAVES STATUS";
    }
    else
    {
        columns = server_version == MYSQL_SERVER_VERSION_55 ? 40 : 38;
        query = "SHOW SLAVE STATUS";
        i_slave_io_running = MYSQL55_STATUS_IO_RUNNING;
        i_slave_sql_running = MYSQL55_STATUS_SQL_RUNNING;
        i_master_log_file = MYSQL55_STATUS_MASTER_LOG_FILE;
        i_read_master_log_pos = MYSQL55_STATUS_MASTER_LOG_POS;
        i_master_server_id = MYSQL55_STATUS_MASTER_ID;
    }

    int64_t master_server_id = SERVER_ID_UNKNOWN;
    int nconfigured = 0;
    int nrunning = 0;

    auto result = execute_query(query);
    if (result.get() == NULL)
    {
        return false;
    }
    else if(result->get_column_count() < columns)
    {
        MXS_ERROR("\"%s\" returned less than the expected amount of columns. "
                  "Expected %u columns.", query, columns);
        return false;
    }

    if (server_version == MYSQL_SERVER_VERSION_100)
    {
        i_slave_io_running = result->get_col_index("Slave_IO_Running");
        i_slave_sql_running = result->get_col_index("Slave_SQL_Running");
        i_master_log_file = result->get_col_index("Master_Log_File");
        i_read_master_log_pos = result->get_col_index("Read_Master_Log_Pos");
        i_master_server_id = result->get_col_index("Master_Server_Id");
        i_slave_rec_hbs = result->get_col_index("Slave_received_heartbeats");
        i_slave_hb_period = result->get_col_index("Slave_heartbeat_period");
        i_master_host = result->get_col_index("Master_Host");
        i_master_port = result->get_col_index("Master_Port");
        i_using_gtid = result->get_col_index("Using_Gtid");
        i_gtid_io_pos = result->get_col_index("Gtid_IO_Pos");
        i_last_io_errno = result->get_col_index("Last_IO_Errno");
        i_last_io_error = result->get_col_index("Last_IO_Error");
        i_last_sql_error = result->get_col_index("Last_SQL_Error");
    }
    // TODO: Add other versions here once it's certain the column names are the same. Better yet, save the
    // indexes to object data so they don't need to be updated every query.

    while (result->next_row())
    {
        nconfigured++;
        /* get Slave_IO_Running and Slave_SQL_Running values*/
        slave_status.slave_io_running = (result->get_string(i_slave_io_running) == "Yes");
        slave_status.slave_sql_running = (result->get_string(i_slave_sql_running) == "Yes");

        if (slave_status.slave_io_running && slave_status.slave_sql_running)
        {
            if (nrunning == 0)
            {
                /** Only check binlog name for the first running slave */
                string master_log_file = result->get_string(i_master_log_file);
                uint64_t read_master_log_pos = result->get_int(i_read_master_log_pos);
                if (slave_status.master_log_file != master_log_file ||
                    slave_status.read_master_log_pos != read_master_log_pos)
                {
                    // IO thread is reading events from the master
                    latest_event = time(NULL);
                }

                slave_status.master_log_file = master_log_file;
                slave_status.read_master_log_pos = read_master_log_pos;
            }
            nrunning++;
        }

        /* If Slave_IO_Running = Yes, assign the master_id to current server: this allows building
         * the replication tree, slaves ids will be added to master(s) and we will have at least the
         * root master server.
         * Please note, there could be no slaves at all if Slave_SQL_Running == 'No'
         */
        int64_t last_io_errno = result->get_int(i_last_io_errno);
        int io_errno = last_io_errno;
        const int connection_errno = 2003;

        if ((io_errno == 0 || io_errno == connection_errno) &&
            server_version != MYSQL_SERVER_VERSION_51)
        {
            /* Get Master_Server_Id */
            master_server_id = scan_server_id(result->get_string(i_master_server_id).c_str());
        }

        if (server_version == MYSQL_SERVER_VERSION_100)
        {
            slave_status.master_host = result->get_string(i_master_host);
            slave_status.master_port = result->get_int(i_master_port);

            string last_io_error = result->get_string(i_last_io_error);
            string last_sql_error = result->get_string(i_last_sql_error);
            slave_status.last_error = !last_io_error.empty() ? last_io_error : last_sql_error;

            int heartbeats = result->get_int(i_slave_rec_hbs);
            if (slave_heartbeats < heartbeats)
            {
                latest_event = time(NULL);
                slave_heartbeats = heartbeats;
                heartbeat_period = result->get_int(i_slave_hb_period);
            }
            string using_gtid = result->get_string(i_using_gtid);
            if (gtid_domain >= 0 && (using_gtid == "Current_Pos" || using_gtid == "Slave_Pos"))
            {
                string gtid_io_pos = result->get_string(i_gtid_io_pos);
                slave_status.gtid_io_pos = !gtid_io_pos.empty() ?
                                                      Gtid(gtid_io_pos.c_str(), gtid_domain) : Gtid();
            }
            else
            {
                slave_status.gtid_io_pos = Gtid();
            }
        }
    }

    if (nconfigured > 0)
    {
        slave_configured = true;
    }
    else
    {
        /** Query returned no rows, replication is not configured */
        slave_configured = false;
        slave_heartbeats = 0;
        slave_status = SlaveStatusInfo();
    }

    slave_status.master_server_id = master_server_id;
    n_slaves_configured = nconfigured;
    n_slaves_running = nrunning;
    return rval;
}
