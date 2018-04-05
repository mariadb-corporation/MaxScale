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
#include <maxscale/thread.h>
#include "utilities.hh"

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
    /* The events_ahead-call below ignores domains where current_pos is ahead of io_pos. This situation is
     * rare but is possible (I guess?) if the server is replicating a domain from multiple masters
     * and decides to process events from one relay log before getting new events to the other. In
     * any case, such events are obsolete and the server can be considered to have processed such logs. */
    return GtidList::events_ahead(slave_status.gtid_io_pos, gtid_current_pos,
                                  GtidList::MISSING_DOMAIN_LHS_ADD);
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

bool MariaDBServer::do_show_slave_status()
{
    /** Column positions for SHOW SLAVE STATUS */
    const size_t MYSQL55_STATUS_MASTER_LOG_POS = 5;
    const size_t MYSQL55_STATUS_MASTER_LOG_FILE = 6;
    const size_t MYSQL55_STATUS_IO_RUNNING = 10;
    const size_t MYSQL55_STATUS_SQL_RUNNING = 11;
    const size_t MYSQL55_STATUS_MASTER_ID = 39;

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
                uint64_t read_master_log_pos = result->get_uint(i_read_master_log_pos);
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
        int64_t last_io_errno = result->get_uint(i_last_io_errno);
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
            slave_status.master_port = result->get_uint(i_master_port);

            string last_io_error = result->get_string(i_last_io_error);
            string last_sql_error = result->get_string(i_last_sql_error);
            slave_status.last_error = !last_io_error.empty() ? last_io_error : last_sql_error;

            int heartbeats = result->get_uint(i_slave_rec_hbs);
            if (slave_heartbeats < heartbeats)
            {
                latest_event = time(NULL);
                slave_heartbeats = heartbeats;
                heartbeat_period = result->get_uint(i_slave_hb_period);
            }
            string using_gtid = result->get_string(i_using_gtid);
            string gtid_io_pos = result->get_string(i_gtid_io_pos);
            if (!gtid_io_pos.empty() &&
                (using_gtid == "Current_Pos" || using_gtid == "Slave_Pos"))
            {
                slave_status.gtid_io_pos = GtidList::from_string(gtid_io_pos);
            }
            else
            {
                slave_status.gtid_io_pos = GtidList();
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
    return true;
}

bool MariaDBServer::update_gtids()
{
    static const string query = "SELECT @@gtid_current_pos, @@gtid_binlog_pos;";
    const int i_current_pos = 0;
    const int i_binlog_pos = 1;

    bool rval = false;
    auto result = execute_query(query);
    if (result.get() != NULL && result->next_row())
    {
        auto current_str = result->get_string(i_current_pos);
        auto binlog_str = result->get_string(i_binlog_pos);
        bool current_ok = false;
        bool binlog_ok = false;
        if (current_str.empty())
        {
            gtid_current_pos = GtidList();
        }
        else
        {
            gtid_current_pos = GtidList::from_string(current_str);
            current_ok = !gtid_current_pos.empty();
        }

        if (binlog_str.empty())
        {
            gtid_binlog_pos = GtidList();
        }
        else
        {
            gtid_binlog_pos = GtidList::from_string(binlog_str);
            binlog_ok = !gtid_binlog_pos.empty();
        }

        rval = (current_ok && binlog_ok);
    }
    return rval;
}

bool MariaDBServer::update_replication_settings()
{
    static const string query = "SELECT @@gtid_strict_mode, @@log_bin, @@log_slave_updates;";
    bool rval = false;
    auto result = execute_query(query);
    if (result.get() != NULL && result->next_row())
    {
        rpl_settings.gtid_strict_mode = result->get_bool(0);
        rpl_settings.log_bin = result->get_bool(1);
        rpl_settings.log_slave_updates = result->get_bool(2);
        rval = true;
    }
    return rval;
}

void MariaDBServer::read_server_variables()
{
    MXS_MONITORED_SERVER* database = server_base;
    string query = "SELECT @@global.server_id, @@read_only;";
    int columns = 2;
    if (version ==  MYSQL_SERVER_VERSION_100)
    {
        query.erase(query.end() - 1);
        query += ", @@global.gtid_domain_id;";
        columns = 3;
    }

    int i_id = 0;
    int i_ro = 1;
    int i_domain = 2;
    auto result = execute_query(query);
    if (result.get() != NULL && result->next_row())
    {
        int64_t server_id_parsed = result->get_uint(i_id);
        if (server_id_parsed < 0)
        {
            server_id_parsed = SERVER_ID_UNKNOWN;
        }
        database->server->node_id = server_id_parsed;
        server_id = server_id_parsed;
        read_only = result->get_bool(i_ro);
        if (columns == 3)
        {
            gtid_domain_id = result->get_uint(i_domain);
        }
    }
}

bool MariaDBServer::check_replication_settings(print_repl_warnings_t print_warnings)
{
    bool rval = true;
    const char* servername = server_base->server->unique_name;
    if (rpl_settings.log_bin == false)
    {
        if (print_warnings == WARNINGS_ON)
        {
            const char NO_BINLOG[] =
                "Slave '%s' has binary log disabled and is not a valid promotion candidate.";
            MXS_WARNING(NO_BINLOG, servername);
        }
        rval = false;
    }
    else if (print_warnings == WARNINGS_ON)
    {
        if (rpl_settings.gtid_strict_mode == false)
        {
            const char NO_STRICT[] =
                "Slave '%s' has gtid_strict_mode disabled. Enabling this setting is recommended. "
                "For more information, see https://mariadb.com/kb/en/library/gtid/#gtid_strict_mode";
            MXS_WARNING(NO_STRICT, servername);
        }
        if (rpl_settings.log_slave_updates == false)
        {
            const char NO_SLAVE_UPDATES[] =
                "Slave '%s' has log_slave_updates disabled. It is a valid candidate but replication "
                "will break for lagging slaves if '%s' is promoted.";
            MXS_WARNING(NO_SLAVE_UPDATES, servername, servername);
        }
    }
    return rval;
}

bool MariaDBServer::wait_until_gtid(const GtidList& target, int timeout, json_t** err_out)
{
    bool gtid_reached = false;
    bool error = false;
    /* Prefer to use gtid_binlog_pos, as that is more reliable. But if log_slave_updates is not on,
     * use gtid_current_pos. */
    const bool use_binlog_pos = rpl_settings.log_bin && rpl_settings.log_slave_updates;

    int seconds_remaining = 1; // Cheat a bit here to allow at least one iteration.
    int sleep_ms = 200; // How long to sleep on next iteration. Incremented slowly.
    time_t start_time = time(NULL);
    while (seconds_remaining > 0 && !gtid_reached && !error)
    {
        if (update_gtids())
        {
            const GtidList& compare_to = use_binlog_pos ? gtid_binlog_pos : gtid_current_pos;
            if (GtidList::events_ahead(target, compare_to, GtidList::MISSING_DOMAIN_IGNORE) == 0)
            {
                gtid_reached = true;
            }
            else
            {
                // Query was successful but target gtid not yet reached. Check elapsed time.
                seconds_remaining = timeout - difftime(time(NULL), start_time);
                if (seconds_remaining > 0)
                {
                    // Sleep for a moment, then try again.
                    thread_millisleep(sleep_ms);
                    sleep_ms += 100; // Sleep a bit more next iteration.
                }
            }
        }
        else
        {
            error = true;
        }
    }

    if (error)
    {
        PRINT_MXS_JSON_ERROR(err_out, "Failed to update gtid on server '%s' while waiting for catchup.",
                             server_base->server->unique_name);
    }
    else if (!gtid_reached)
    {
        PRINT_MXS_JSON_ERROR(err_out, "Slave catchup timed out on slave '%s'.",
                             server_base->server->unique_name);
    }
    return gtid_reached;
}
