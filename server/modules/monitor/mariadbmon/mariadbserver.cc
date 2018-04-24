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

#include <fstream>
#include <inttypes.h>
#include <sstream>
#include <maxscale/mysql_utils.h>
#include <maxscale/thread.h>

using std::string;

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
{
    ss_dassert(monitored_server);
}

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
            auto parsed = result->get_uint(i_master_server_id);
            if (parsed >= 0)
            {
                master_server_id = parsed;
            }
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
    const char* servername = name();
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
                             name());
    }
    else if (!gtid_reached)
    {
        PRINT_MXS_JSON_ERROR(err_out, "Slave catchup timed out on slave '%s'.", name());
    }
    return gtid_reached;
}

bool MariaDBServer::is_master() const
{
    return SERVER_IS_MASTER(server_base->server);
}

bool MariaDBServer::is_slave() const
{
    return SERVER_IS_SLAVE(server_base->server);
}

bool MariaDBServer::is_running() const
{
    return SERVER_IS_RUNNING(server_base->server);
}

bool MariaDBServer::is_down() const
{
    return SERVER_IS_DOWN(server_base->server);
}

const char* MariaDBServer::name() const
{
    return server_base->server->unique_name;
}

string MariaDBServer::diagnostics(bool multimaster) const
{
    std::stringstream ss;
    ss << "Server:                 " << name() << "\n";
    ss << "Server ID:              " << server_id << "\n";
    ss << "Read only:              " << (read_only ? "YES" : "NO") << "\n";
    ss << "Slave configured:       " << (slave_configured ? "YES" : "NO") << "\n";
    if (slave_configured)
    {
        ss << "Slave IO running:       " << (slave_status.slave_io_running ? "YES" : "NO") << "\n";
        ss << "Slave SQL running:      " << (slave_status.slave_sql_running ? "YES" : "NO") << "\n";
        ss << "Master ID:              " << slave_status.master_server_id << "\n";
        ss << "Master binlog file:     " << slave_status.master_log_file << "\n";
        ss << "Master binlog position: " << slave_status.read_master_log_pos << "\n";
    }
    if (!gtid_current_pos.empty())
    {
        ss << "Gtid current position:  " << gtid_current_pos.to_string() << "\n";
    }
    if (!gtid_binlog_pos.empty())
    {
        ss << "Gtid binlog position:   " << gtid_binlog_pos.to_string() << "\n";
    }
    if (!slave_status.gtid_io_pos.empty())
    {
        ss << "Gtid slave IO position: " << slave_status.gtid_io_pos.to_string() << "\n";
    }
    if (multimaster)
    {
        ss << "Master group:           " << group << "\n";
    }
    return ss.str();
}

json_t* MariaDBServer::diagnostics_json(bool multimaster) const
{
    json_t* srv = json_object();
    json_object_set_new(srv, "name", json_string(name()));
    json_object_set_new(srv, "server_id", json_integer(server_id));
    json_object_set_new(srv, "master_id", json_integer(slave_status.master_server_id));

    json_object_set_new(srv, "read_only", json_boolean(read_only));
    json_object_set_new(srv, "slave_configured", json_boolean(slave_configured));
    json_object_set_new(srv, "slave_io_running", json_boolean(slave_status.slave_io_running));
    json_object_set_new(srv, "slave_sql_running", json_boolean(slave_status.slave_sql_running));

    json_object_set_new(srv, "master_binlog_file", json_string(slave_status.master_log_file.c_str()));
    json_object_set_new(srv, "master_binlog_position", json_integer(slave_status.read_master_log_pos));
    json_object_set_new(srv, "gtid_current_pos", json_string(gtid_current_pos.to_string().c_str()));
    json_object_set_new(srv, "gtid_binlog_pos", json_string(gtid_binlog_pos.to_string().c_str()));
    json_object_set_new(srv, "gtid_io_pos", json_string(slave_status.gtid_io_pos.to_string().c_str()));
    if (multimaster)
    {
        json_object_set_new(srv, "master_group", json_integer(group));
    }
    return srv;
}

bool MariaDBServer::uses_gtid(json_t** error_out)
{
    bool using_gtid = !slave_status.gtid_io_pos.empty();
    if (!using_gtid)
    {
        string slave_not_gtid_msg = string("Slave server ") + name() + " is not using gtid replication.";
        PRINT_MXS_JSON_ERROR(error_out, "%s", slave_not_gtid_msg.c_str());
    }
    return using_gtid;
}

bool MariaDBServer::update_slave_info()
{
    return (slave_status.slave_sql_running && update_replication_settings() &&
            update_gtids() && do_show_slave_status());
}

bool MariaDBServer::can_replicate_from(MariaDBServer* master)
{
    bool rval = false;
    if (update_gtids())
    {
        rval = gtid_current_pos.can_replicate_from(master->gtid_binlog_pos);
    }
    return rval;
}

bool MariaDBServer::redirect_one_slave(const string& change_cmd)
{
    bool success = false;
    MYSQL* slave_conn = server_base->con;
    const char* query = "STOP SLAVE;";
    if (mxs_mysql_query(slave_conn, query) == 0)
    {
        query = "RESET SLAVE;"; // To erase any old I/O or SQL errors
        if (mxs_mysql_query(slave_conn, query) == 0)
        {
            query = "CHANGE MASTER TO ..."; // Don't show the real query as it contains a password.
            if (mxs_mysql_query(slave_conn, change_cmd.c_str()) == 0)
            {
                query = "START SLAVE;";
                if (mxs_mysql_query(slave_conn, query) == 0)
                {
                    success = true;
                    MXS_NOTICE("Slave '%s' redirected to new master.", name());
                }
            }
        }
    }

    if (!success)
    {
        MXS_WARNING("Slave '%s' redirection failed: '%s'. Query: '%s'.", name(),
                    mysql_error(slave_conn), query);
    }
    return success;
}

bool MariaDBServer::join_cluster(const string& change_cmd)
{
    /* Server does not have slave connections. This operation can fail, or the resulting
     * replication may end up broken. */
    bool success = false;
    string error_msg;
    MYSQL* server_conn = server_base->con;
    const char* query = "SET GLOBAL read_only=1;";
    if (mxs_mysql_query(server_conn, query) == 0)
    {
        query = "CHANGE MASTER TO ..."; // Don't show the real query as it contains a password.
        if (mxs_mysql_query(server_conn, change_cmd.c_str()) == 0)
        {
            query = "START SLAVE;";
            if (mxs_mysql_query(server_conn, query) == 0)
            {
                success = true;
                MXS_NOTICE("Standalone server '%s' starting replication.", name());
            }
        }

        if (!success)
        {
            // A step after "SET GLOBAL read_only=1" failed, try to undo. First, backup error message.
            error_msg = mysql_error(server_conn);
            mxs_mysql_query(server_conn, "SET GLOBAL read_only=0;");
        }
    }

    if (!success)
    {
        if (error_msg.empty())
        {
            error_msg = mysql_error(server_conn);
        }
        MXS_WARNING("Standalone server '%s' failed to start replication: '%s'. Query: '%s'.",
                    name(), error_msg.c_str(), query);
    }
    return success;
}

bool MariaDBServer::failover_wait_relay_log(int seconds_remaining, json_t** err_out)
{
    time_t begin = time(NULL);
    bool query_ok = true;
    bool io_pos_stable = true;
    while (relay_log_events() > 0 &&
           query_ok &&
           io_pos_stable &&
           difftime(time(NULL), begin) < seconds_remaining)
    {
        MXS_INFO("Relay log of server '%s' not yet empty, waiting to clear %" PRId64 " events.",
                 name(), relay_log_events());
        thread_millisleep(1000); // Sleep for a while before querying server again.
        // Todo: check server version before entering failover.
        GtidList old_gtid_io_pos = slave_status.gtid_io_pos;
        // Update gtid:s first to make sure Gtid_IO_Pos is the more recent value.
        // It doesn't matter here, but is a general rule.
        query_ok = update_gtids() && do_show_slave_status();
        io_pos_stable = (old_gtid_io_pos == slave_status.gtid_io_pos);
    }

    bool rval = false;
    if (relay_log_events() == 0)
    {
        rval = true;
    }
    else
    {
        string reason = "Timeout";
        if (!query_ok)
        {
            reason = "Query error";
        }
        else if (!io_pos_stable)
        {
            reason = "Old master sent new event(s)";
        }
        else if (relay_log_events() < 0) // TODO: This is currently impossible
        {
            reason = "Invalid Gtid(s) (current_pos: " + gtid_current_pos.to_string() +
                     ", io_pos: " + slave_status.gtid_io_pos.to_string() + ")";
        }
        PRINT_MXS_JSON_ERROR(err_out, "Failover: %s while waiting for server '%s' to process relay log. "
                             "Cancelling failover.", reason.c_str(), name());
        rval = false;
    }
    return rval;
}

bool MariaDBServer::run_sql_from_file(const string& path, json_t** error_out)
{
    MYSQL* conn = server_base->con;
    bool error = false;
    std::ifstream sql_file(path);
    if (sql_file.is_open())
    {
        MXS_NOTICE("Executing sql queries from file '%s' on server '%s'.", path.c_str(), name());
        int lines_executed = 0;

        while (!sql_file.eof() && !error)
        {
            string line;
            std::getline(sql_file, line);
            if (sql_file.bad())
            {
                PRINT_MXS_JSON_ERROR(error_out, "Error when reading sql text file '%s': '%s'.",
                                     path.c_str(), mxs_strerror(errno));
                error = true;
            }
            // Skip empty lines and comment lines
            else if (!line.empty() && line[0] != '#')
            {
                if (mxs_mysql_query(conn, line.c_str()) == 0)
                {
                    lines_executed++;
                    // Discard results if any.
                    MYSQL_RES* res = mysql_store_result(conn);
                    if (res != NULL)
                    {
                        mysql_free_result(res);
                    }
                }
                else
                {
                    PRINT_MXS_JSON_ERROR(error_out, "Failed to execute sql from text file '%s'. Query: '%s'. "
                                         "Error: '%s'.", path.c_str(), line.c_str(), mysql_error(conn));
                    error = true;
                }
            }
        }
        MXS_NOTICE("%d queries executed successfully.", lines_executed);
    }
    else
    {
        PRINT_MXS_JSON_ERROR(error_out, "Could not open sql text file '%s'.", path.c_str());
        error = true;
    }
    return !error;
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
    if (data && *data)
    {
        errno = 0; // strtoll sets this
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
    ss_dassert(column_ind < m_columns);
    char* data = m_rowdata[column_ind];
    return data ? (strcmp(data,"Y") == 0 || strcmp(data, "1") == 0) : false;
}
