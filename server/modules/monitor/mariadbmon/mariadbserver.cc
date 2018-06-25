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

#include "mariadbserver.hh"

#include <fstream>
#include <inttypes.h>
#include <iomanip>
#include <sstream>
#include <maxscale/mysql_utils.h>
#include <maxscale/thread.h>

using std::string;

namespace
{
    // Used for Slave_IO_Running
    const char YES[] = "Yes";
    const char PREPARING[] = "Preparing";
    const char CONNECTING[] = "Connecting";
    const char NO[] = "No";
}

SlaveStatus::SlaveStatus()
    : master_server_id(SERVER_ID_UNKNOWN)
    , master_port(PORT_UNKNOWN)
    , slave_io_running(SLAVE_IO_NO)
    , slave_sql_running(false)
{}

MariaDBServer::MariaDBServer(MXS_MONITORED_SERVER* monitored_server, int config_index)
    : m_server_base(monitored_server)
    , m_config_index(config_index)
    , m_print_update_errormsg(true)
    , m_version(version::UNKNOWN)
    , m_server_id(SERVER_ID_UNKNOWN)
    , m_read_only(false)
    , m_n_slaves_running(0)
    , m_n_slave_heartbeats(0)
    , m_heartbeat_period(0)
    , m_latest_event(0)
    , m_gtid_domain_id(GTID_DOMAIN_UNKNOWN)
{
    ss_dassert(monitored_server);
}

NodeData::NodeData()
    : index(INDEX_NOT_VISITED)
    , lowest_index(INDEX_NOT_VISITED)
    , in_stack(false)
    , cycle(CYCLE_NONE)
    , reach(REACH_UNKNOWN)
{}

void NodeData::reset_results()
{
    cycle = CYCLE_NONE;
    reach = REACH_UNKNOWN;
    parents.clear();
    children.clear();
    external_masters.clear();
}

void NodeData::reset_indexes()
{
    index = INDEX_NOT_VISITED;
    lowest_index = INDEX_NOT_VISITED;
    in_stack = false;
}

int64_t MariaDBServer::relay_log_events()
{
    /* The events_ahead-call below ignores domains where current_pos is ahead of io_pos. This situation is
     * rare but is possible (I guess?) if the server is replicating a domain from multiple masters
     * and decides to process events from one relay log before getting new events to the other. In
     * any case, such events are obsolete and the server can be considered to have processed such logs. */
    // TODO: Fix for multisource repl
    return !m_slave_status.empty() ? GtidList::events_ahead(m_slave_status[0].gtid_io_pos, m_gtid_current_pos,
                                                            GtidList::MISSING_DOMAIN_LHS_ADD) : 0;
}

std::auto_ptr<QueryResult> MariaDBServer::execute_query(const string& query, std::string* errmsg_out)
{
    auto conn = m_server_base->con;
    std::auto_ptr<QueryResult> rval;
    MYSQL_RES *result = NULL;
    if (mxs_mysql_query(conn, query.c_str()) == 0 && (result = mysql_store_result(conn)) != NULL)
    {
        rval = std::auto_ptr<QueryResult>(new QueryResult(result));
    }
    else if (errmsg_out)
    {
        *errmsg_out = string("Query '") + query + "' failed: '" + mysql_error(conn) + "'.";
    }
    return rval;
}

bool MariaDBServer::do_show_slave_status(string* errmsg_out)
{
    unsigned int columns = 0;
    string query;
    bool all_slaves_status = false;
    switch (m_version)
    {
        case version::MARIADB_100:
        case version::BINLOG_ROUTER:
            columns = 42;
            all_slaves_status = true;
            query = "SHOW ALL SLAVES STATUS";
            break;
        case version::MARIADB_MYSQL_55:
            columns = 40;
            query = "SHOW SLAVE STATUS";
            break;
        default:
            ss_dassert(!true); // This method should not be called for versions < 5.5
            return false;
    }

    auto result = execute_query(query, errmsg_out);
    if (result.get() == NULL)
    {
        return false;
    }
    else if(result->get_column_count() < columns)
    {
        MXS_ERROR("'%s' returned less than the expected amount of columns. Expected %u columns, "
                  "got %" PRId64 ".", query.c_str(), columns, result->get_column_count());
        return false;
    }

    // Fields common to all server versions
    auto i_master_host = result->get_col_index("Master_Host");
    auto i_master_port = result->get_col_index("Master_Port");
    auto i_slave_io_running = result->get_col_index("Slave_IO_Running");
    auto i_slave_sql_running = result->get_col_index("Slave_SQL_Running");
    auto i_master_server_id = result->get_col_index("Master_Server_Id");
    auto i_last_io_errno = result->get_col_index("Last_IO_Errno");
    auto i_last_io_error = result->get_col_index("Last_IO_Error");
    auto i_last_sql_error = result->get_col_index("Last_SQL_Error");

    const char INVALID_DATA[] = "'%s' returned invalid data.";
    if (i_master_host < 0 || i_master_port < 0 || i_slave_io_running < 0 || i_slave_sql_running < 0 ||
        i_master_server_id < 0 || i_last_io_errno < 0  || i_last_io_error < 0 || i_last_sql_error < 0)
    {
        MXS_ERROR(INVALID_DATA, query.c_str());
        return false;
    }

    int64_t i_connection_name = -1, i_slave_rec_hbs = -1, i_slave_hb_period = -1;
    int64_t i_using_gtid = -1, i_gtid_io_pos = -1;
    if (all_slaves_status)
    {
        i_connection_name = result->get_col_index("Connection_name");
        i_slave_rec_hbs = result->get_col_index("Slave_received_heartbeats");
        i_slave_hb_period = result->get_col_index("Slave_heartbeat_period");
        i_using_gtid = result->get_col_index("Using_Gtid");
        i_gtid_io_pos = result->get_col_index("Gtid_IO_Pos");
        if (i_connection_name < 0 || i_slave_rec_hbs < 0 || i_slave_hb_period < 0 ||
            i_using_gtid < 0 || i_gtid_io_pos < 0)
        {
            MXS_ERROR(INVALID_DATA, query.c_str());
            return false;
        }
    }

    m_slave_status.clear();
    int nrunning = 0;
    while (result->next_row())
    {
        SlaveStatus sstatus;
        sstatus.master_host = result->get_string(i_master_host);
        sstatus.master_port = result->get_uint(i_master_port);
        string last_io_error = result->get_string(i_last_io_error);
        string last_sql_error = result->get_string(i_last_sql_error);
        sstatus.last_error = !last_io_error.empty() ? last_io_error : last_sql_error;

        sstatus.slave_io_running =
            SlaveStatus::slave_io_from_string(result->get_string(i_slave_io_running));
        sstatus.slave_sql_running = (result->get_string(i_slave_sql_running) == "Yes");

        if (sstatus.slave_io_running == SlaveStatus::SLAVE_IO_YES)
        {
            // TODO: Fix for multisource replication, check changes to IO_Pos here and save somewhere.
            sstatus.master_server_id = result->get_uint(i_master_server_id);
            if (sstatus.slave_sql_running)
            {
                nrunning++;
            }
        }

        if (all_slaves_status)
        {
            sstatus.name = result->get_string(i_connection_name);
            auto heartbeats = result->get_uint(i_slave_rec_hbs);
            if (m_n_slave_heartbeats < heartbeats) // TODO: Fix for multisource replication
            {
                m_latest_event = time(NULL);
                m_n_slave_heartbeats = heartbeats;
                m_heartbeat_period = result->get_uint(i_slave_hb_period);
            }
            string using_gtid = result->get_string(i_using_gtid);
            string gtid_io_pos = result->get_string(i_gtid_io_pos);
            if (!gtid_io_pos.empty() &&
                (using_gtid == "Current_Pos" || using_gtid == "Slave_Pos"))
            {
                sstatus.gtid_io_pos = GtidList::from_string(gtid_io_pos);
            }
        }
        m_slave_status.push_back(sstatus);
    }

    if (m_slave_status.empty())
    {
        /** Query returned no rows, replication is not configured */
        m_n_slave_heartbeats = 0;
    }

    m_n_slaves_running = nrunning;
    return true;
}

bool MariaDBServer::update_gtids(string* errmsg_out)
{
    static const string query = "SELECT @@gtid_current_pos, @@gtid_binlog_pos;";
    const int i_current_pos = 0;
    const int i_binlog_pos = 1;

    bool rval = false;
    auto result = execute_query(query, errmsg_out);
    if (result.get() != NULL && result->next_row())
    {
        auto current_str = result->get_string(i_current_pos);
        auto binlog_str = result->get_string(i_binlog_pos);
        bool current_ok = false;
        if (current_str.empty())
        {
            m_gtid_current_pos = GtidList();
        }
        else
        {
            m_gtid_current_pos = GtidList::from_string(current_str);
            current_ok = !m_gtid_current_pos.empty();
        }

        if (binlog_str.empty())
        {
            m_gtid_binlog_pos = GtidList();
        }
        else
        {
            m_gtid_binlog_pos = GtidList::from_string(binlog_str);
        }

        rval = current_ok;
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
        m_rpl_settings.gtid_strict_mode = result->get_bool(0);
        m_rpl_settings.log_bin = result->get_bool(1);
        m_rpl_settings.log_slave_updates = result->get_bool(2);
        rval = true;
    }
    return rval;
}

bool MariaDBServer::read_server_variables(string* errmsg_out)
{
    MXS_MONITORED_SERVER* database = m_server_base;
    string query = "SELECT @@global.server_id, @@read_only;";
    int columns = 2;
    if (m_version ==  version::MARIADB_100)
    {
        query.erase(query.end() - 1);
        query += ", @@global.gtid_domain_id;";
        columns = 3;
    }

    int i_id = 0;
    int i_ro = 1;
    int i_domain = 2;
    bool rval = false;
    auto result = execute_query(query, errmsg_out);
    if (result.get() != NULL && result->next_row())
    {
        rval = true;
        int64_t server_id_parsed = result->get_uint(i_id);
        if (server_id_parsed < 0) // This is very unlikely, requiring an error in server or connector.
        {
            server_id_parsed = SERVER_ID_UNKNOWN;
            rval = false;
        }
        database->server->node_id = server_id_parsed;
        m_server_id = server_id_parsed;
        m_read_only = result->get_bool(i_ro);
        if (columns == 3)
        {
            int64_t domain_id_parsed = result->get_uint(i_domain);
            if (domain_id_parsed < 0) // Same here.
            {
                domain_id_parsed = GTID_DOMAIN_UNKNOWN;
                rval = false;
            }
            m_gtid_domain_id = domain_id_parsed;
        }
        else
        {
            m_gtid_domain_id = GTID_DOMAIN_UNKNOWN;
        }
    }
    return rval;
}

bool MariaDBServer::check_replication_settings(print_repl_warnings_t print_warnings)
{
    bool rval = true;
    const char* servername = name();
    if (m_rpl_settings.log_bin == false)
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
        if (m_rpl_settings.gtid_strict_mode == false)
        {
            const char NO_STRICT[] =
                "Slave '%s' has gtid_strict_mode disabled. Enabling this setting is recommended. "
                "For more information, see https://mariadb.com/kb/en/library/gtid/#gtid_strict_mode";
            MXS_WARNING(NO_STRICT, servername);
        }
        if (m_rpl_settings.log_slave_updates == false)
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
    const bool use_binlog_pos = m_rpl_settings.log_bin && m_rpl_settings.log_slave_updates;

    int seconds_remaining = 1; // Cheat a bit here to allow at least one iteration.
    int sleep_ms = 200; // How long to sleep on next iteration. Incremented slowly.
    time_t start_time = time(NULL);
    while (seconds_remaining > 0 && !gtid_reached && !error)
    {
        if (update_gtids())
        {
            const GtidList& compare_to = use_binlog_pos ? m_gtid_binlog_pos : m_gtid_current_pos;
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
    // Similar to macro SERVER_IS_MASTER
    return SRV_MASTER_STATUS(m_server_base->pending_status);
}

bool MariaDBServer::is_slave() const
{
    // Similar to macro SERVER_IS_SLAVE
    return (m_server_base->pending_status & (SERVER_RUNNING | SERVER_SLAVE | SERVER_MAINT)) ==
           (SERVER_RUNNING | SERVER_SLAVE);
}

bool MariaDBServer::is_running() const
{
    // Similar to macro SERVER_IS_RUNNING
    return (m_server_base->pending_status & (SERVER_RUNNING | SERVER_MAINT)) == SERVER_RUNNING;
}

bool MariaDBServer::is_down() const
{
    // Similar to macro SERVER_IS_DOWN
    return (m_server_base->pending_status & SERVER_RUNNING) == 0;
}

bool MariaDBServer::is_in_maintenance() const
{
    return m_server_base->pending_status & SERVER_MAINT;
}

bool MariaDBServer::is_relay_server() const
{
    return (m_server_base->pending_status & (SERVER_RUNNING | SERVER_MASTER | SERVER_SLAVE | SERVER_MAINT)) ==
           (SERVER_RUNNING | SERVER_MASTER | SERVER_SLAVE);
}

bool MariaDBServer::has_status(uint64_t bits) const
{
    return (m_server_base->pending_status & bits) == bits;
}

bool MariaDBServer::had_status(uint64_t bits) const
{
    return (m_server_base->mon_prev_status & bits) == bits;
}

bool MariaDBServer::is_read_only() const
{
    return m_read_only;
}

const char* MariaDBServer::name() const
{
    return m_server_base->server->name;
}

string MariaDBServer::diagnostics(bool multimaster) const
{
    std::stringstream ss;
    ss << "Server:                 " << name() << "\n";
    ss << "Server ID:              " << m_server_id << "\n";
    ss << "Read only:              " << (m_read_only ? "Yes" : "No") << "\n";
    ss << (m_slave_status.empty() ? "No slave connections \n" : "Slave connections: \n");

    for (auto iter = m_slave_status.begin(); iter != m_slave_status.end(); iter++)
    {
        ss << iter->to_string();
    }
    if (!m_gtid_current_pos.empty())
    {
        ss << "Gtid current position:  " << m_gtid_current_pos.to_string() << "\n";
    }
    if (!m_gtid_binlog_pos.empty())
    {
        ss << "Gtid binlog position:   " << m_gtid_binlog_pos.to_string() << "\n";
    }
    if (multimaster)
    {
        ss << "Master group:           " << m_node.cycle << "\n";
    }
    return ss.str();
}

json_t* MariaDBServer::diagnostics_json(bool multimaster) const
{
    json_t* srv = json_object();
    json_object_set_new(srv, "name", json_string(name()));
    json_object_set_new(srv, "server_id", json_integer(m_server_id));
    json_object_set_new(srv, "read_only", json_boolean(m_read_only));
    json_object_set_new(srv, "slave_configured", json_boolean(!m_slave_status.empty()));
    if (!m_slave_status.empty())
    {
        json_object_set_new(srv, "slave_io_running",
            json_string(SlaveStatus::slave_io_to_string(m_slave_status[0].slave_io_running).c_str()));
        json_object_set_new(srv, "slave_sql_running", json_boolean(m_slave_status[0].slave_sql_running));
        json_object_set_new(srv, "master_id", json_integer(m_slave_status[0].master_server_id));
    }
    if (!m_gtid_current_pos.empty())
    {
        json_object_set_new(srv, "gtid_current_pos", json_string(m_gtid_current_pos.to_string().c_str()));
    }
    if (!m_gtid_binlog_pos.empty())
    {
        json_object_set_new(srv, "gtid_binlog_pos", json_string(m_gtid_binlog_pos.to_string().c_str()));
    }
    if (!m_slave_status.empty() && !m_slave_status[0].gtid_io_pos.empty())
    {
        json_object_set_new(srv, "gtid_io_pos",
                            json_string(m_slave_status[0].gtid_io_pos.to_string().c_str()));
    }
    if (multimaster)
    {
        json_object_set_new(srv, "master_group", json_integer(m_node.cycle));
    }
    return srv;
}

bool MariaDBServer::uses_gtid(std::string* error_out)
{
    bool using_gtid = !m_slave_status.empty() && !m_slave_status[0].gtid_io_pos.empty();
    if (!using_gtid)
    {
        *error_out = string("Slave server ") + name() + " is not using gtid replication.";
    }
    return using_gtid;
}

bool MariaDBServer::update_slave_info()
{
    // TODO: fix for multisource repl
    return (!m_slave_status.empty() && m_slave_status[0].slave_sql_running && update_replication_settings() &&
            update_gtids() && do_show_slave_status());
}

bool MariaDBServer::can_replicate_from(MariaDBServer* master, string* error_out)
{
    bool rval = false;
    if (update_gtids())
    {
        if (m_gtid_current_pos.empty())
        {
            *error_out = string("'") + name() + "' does not have a valid 'gtid_current_pos'.";
        }
        else if (master->m_gtid_binlog_pos.empty())
        {
            *error_out = string("'") + master->name() + "' does not have a valid 'gtid_binlog_pos'.";
        }
        else
        {
            rval = m_gtid_current_pos.can_replicate_from(master->m_gtid_binlog_pos);
            if (!rval)
            {
                *error_out = string("gtid_current_pos of '") + name() + "' (" +
                    m_gtid_current_pos.to_string() + ") is incompatible with gtid_binlog_pos of '" +
                    master->name() + "' (" + master->m_gtid_binlog_pos.to_string() + ").";
            }
        }
    }
    else
    {
        *error_out = string("Server '") + name() + "' could not be queried.";
    }
    return rval;
}

bool MariaDBServer::redirect_one_slave(const string& change_cmd)
{
    bool success = false;
    MYSQL* slave_conn = m_server_base->con;
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
    MYSQL* server_conn = m_server_base->con;
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
    }

    if (!success)
    {
        const char ERROR_MSG[] = "Standalone server '%s' failed to start replication: '%s'. Query: '%s'.";
        MXS_WARNING(ERROR_MSG, name(), mysql_error(server_conn), query);
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
        // TODO: check server version before entering failover.
        // TODO: fix for multisource
        GtidList old_gtid_io_pos = m_slave_status[0].gtid_io_pos;
        // Update gtid:s first to make sure Gtid_IO_Pos is the more recent value.
        // It doesn't matter here, but is a general rule.
        query_ok = update_gtids() && do_show_slave_status();
        io_pos_stable = (old_gtid_io_pos == m_slave_status[0].gtid_io_pos);
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
            reason = "Invalid Gtid(s) (current_pos: " + m_gtid_current_pos.to_string() +
                     ", io_pos: " + m_slave_status[0].gtid_io_pos.to_string() + ")";
        }
        PRINT_MXS_JSON_ERROR(err_out, "Failover: %s while waiting for server '%s' to process relay log. "
                             "Cancelling failover.", reason.c_str(), name());
        rval = false;
    }
    return rval;
}

bool MariaDBServer::run_sql_from_file(const string& path, json_t** error_out)
{
    MYSQL* conn = m_server_base->con;
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

/**
 * Query this server.
 */
void MariaDBServer::monitor_server()
{
    string errmsg;
    bool query_ok = false;
    /* Query different things depending on server version/type. */
    switch (m_version)
    {
        case version::MARIADB_MYSQL_55:
            query_ok = read_server_variables(&errmsg) && update_slave_status(&errmsg);
            break;
        case version::MARIADB_100:
            query_ok = read_server_variables(&errmsg) && update_gtids(&errmsg) &&
                       update_slave_status(&errmsg);
            break;
        case version::BINLOG_ROUTER:
            // TODO: Add special version of server variable query.
            query_ok = update_slave_status(&errmsg);
            break;
        default:
            // Do not update unknown versions.
            query_ok = true;
            break;
    }

    if (query_ok)
    {
        m_print_update_errormsg = true;
    }
    /* If one of the queries ran to an error, print the error message, assuming it hasn't already been
     * printed. Some really unlikely errors won't produce an error message, but these are visible in other
     * ways. */
    else if (!errmsg.empty() && m_print_update_errormsg)
    {
        MXS_WARNING("Error during monitor update of server '%s'. %s.", name(), errmsg.c_str());
        m_print_update_errormsg = false;
    }
    return;
}

/**
 * Update slave status of the server.
 *
 * @param errmsg_out Where to store an error message if query fails. Can be null.
 * @return True on success
 */
bool MariaDBServer::update_slave_status(string* errmsg_out)
{
    /** Clear old states */
    clear_status(SERVER_SLAVE | SERVER_MASTER | SERVER_RELAY_MASTER | SERVER_SLAVE_OF_EXT_MASTER);

    bool rval = false;
    if (do_show_slave_status(errmsg_out))
    {
        rval = true;
        /* If all configured slaves are running set this node as slave */
        if (m_n_slaves_running > 0 && m_n_slaves_running == m_slave_status.size())
        {
            set_status(SERVER_SLAVE);
        }

        /** Store master_id of current node. */
        m_server_base->server->master_id = !m_slave_status.empty() ?
            m_slave_status[0].master_server_id : SERVER_ID_UNKNOWN;
    }
    return rval;
}

/**
 * Update information which changes rarely. This method should be called after (re)connecting to a backend.
 * Calling this every monitoring loop is overkill.
 */
void MariaDBServer::update_server_version()
{
    m_version = version::UNKNOWN;
    auto conn = m_server_base->con;
    auto srv = m_server_base->server;

    /* Get server version string, also get/set numeric representation. This function does not query the
     * server, since the data was obtained when connecting. */
    mxs_mysql_set_server_version(conn, srv);

    // Check whether this server is a MaxScale Binlog Server.
    MYSQL_RES *result;
    if (mxs_mysql_query(conn, "SELECT @@maxscale_version") == 0 &&
        (result = mysql_store_result(conn)) != NULL)
    {
        m_version = version::BINLOG_ROUTER;
        mysql_free_result(result);
    }
    else
    {
        /* Not a binlog server, check version number. */
        uint64_t version_num = server_get_version(srv);
        if (version_num >= 100000 && srv->server_type == SERVER_TYPE_MARIADB)
        {
            m_version = version::MARIADB_100;
        }
        else if (version_num >= 5 * 10000 + 5 * 100)
        {
            m_version = version::MARIADB_MYSQL_55;
        }
        else
        {
            MXS_ERROR("MariaDB/MySQL version of server '%s' is less than 5.5, which is not supported. "
                      "The server is ignored by the monitor. Server version: '%s'.", name(),
                      srv->version_string);
        }
    }
}

/**
 * Checks monitor permissions on the server. Sets/clears the SERVER_AUTH_ERROR bit.
 */
void MariaDBServer::check_permissions()
{
    MYSQL* conn = m_server_base->con;
    // Test with a typical query to make sure the monitor has sufficient permissions.
    const char* query = "SHOW SLAVE STATUS;";
    if (mxs_mysql_query(conn, query) != 0)
    {
        /* In theory, this could be due to other errors as well, but that is quite unlikely since the
         * connection was just checked. The end result is in any case that the server is not updated,
         * and that this test is retried next round. */
        set_status(SERVER_AUTH_ERROR);
        // Only print error if last round was ok.
        if (!had_status(SERVER_AUTH_ERROR))
        {
            MXS_WARNING("Query '%s' to server '%s' failed when checking monitor permissions: '%s'. ",
                        query, name(), mysql_error(conn));
        }
    }
    else
    {
        clear_status(SERVER_AUTH_ERROR);
        MYSQL_RES* result = mysql_store_result(conn);
        if (result)
        {
            mysql_free_result(result);
        }
    }
}

void MariaDBServer::clear_status(uint64_t bits)
{
    monitor_clear_pending_status(m_server_base, bits);
}

void MariaDBServer::set_status(uint64_t bits)
{
    monitor_set_pending_status(m_server_base, bits);
}

string SlaveStatus::to_string() const
{
    using std::setw;
    // Print all of this on the same line to make things compact. Are the widths reasonable? The format is
    // not quite array-like since usually there is just one row. May be changed later.
    std::stringstream result;
    std::stringstream helper;
    helper << "[" << master_host << "]:" << master_port;
    result << "  Host: " << setw(22) << helper.str() << ", "; // small intendation
    string host_port = slave_io_to_string(slave_io_running) + "/" + (slave_sql_running ? "Yes" : "No");
    result << "IO/SQL running: " << setw(7) << host_port << ", ";
    result << "Master ID: " << setw(4) << master_server_id << ", ";
    result << "Gtid_IO_Pos: " << gtid_io_pos.to_string() << "\n";
    return result.str();
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
    else if (str !=  NO)
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
            ss_dassert(!false);
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
    ss_dassert(column_ind < m_columns && column_ind >= 0);
    char* data = m_rowdata[column_ind];
    return data ? data : "";
}

int64_t QueryResult::get_uint(int64_t column_ind) const
{
    ss_dassert(column_ind < m_columns && column_ind >= 0);
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
    ss_dassert(column_ind < m_columns && column_ind >= 0);
    char* data = m_rowdata[column_ind];
    return data ? (strcmp(data,"Y") == 0 || strcmp(data, "1") == 0) : false;
}
