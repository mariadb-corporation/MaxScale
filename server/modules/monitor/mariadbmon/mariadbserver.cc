/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mariadbserver.hh"

#include <fstream>
#include <inttypes.h>
#include <set>
#include <mysql.h>
#include <mysqld_error.h>
#include <maxbase/format.hh>
#include <maxsql/mariadb.hh>
#include <maxscale/mysql_utils.hh>

using std::string;
using maxbase::string_printf;
using maxbase::Duration;
using maxbase::StopWatch;
using maxsql::QueryResult;
using Guard = std::lock_guard<std::mutex>;
using maxscale::MonitorServer;
using ConnectResult = maxscale::MonitorServer::ConnectResult;
using namespace std::chrono_literals;

namespace
{
const char not_a_db[] = "it is not a valid database.";
}

MariaDBServer::MariaDBServer(SERVER* server, int config_index,
                             const MonitorServer::SharedSettings& base_settings,
                             const MariaDBServer::SharedSettings& settings)
    : MonitorServer(server, base_settings)
    , m_config_index(config_index)
    , m_settings(settings)
{
}

NodeData::NodeData()
    : index(INDEX_NOT_VISITED)
    , lowest_index(INDEX_NOT_VISITED)
    , in_stack(false)
    , cycle(CYCLE_NONE)
    , reach(REACH_UNKNOWN)
{
}

void NodeData::reset_results()
{
    cycle = CYCLE_NONE;
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

uint64_t MariaDBServer::relay_log_events(const SlaveStatus& slave_conn)
{
    /* The events_ahead-call below ignores domains where current_pos is ahead of io_pos. This situation is
     * rare but is possible (I guess?) if the server is replicating a domain from multiple masters
     * and decides to process events from one relay log before getting new events to the other. In
     * any case, such events are obsolete and the server can be considered to have processed such logs. */
    return slave_conn.gtid_io_pos.events_ahead(m_gtid_current_pos, GtidList::MISSING_DOMAIN_IGNORE);
}

std::unique_ptr<QueryResult> MariaDBServer::execute_query(const string& query, string* errmsg_out,
                                                          unsigned int* errno_out)
{
    return maxscale::execute_query(con, query, errmsg_out, errno_out);
}

/**
 * Execute a query which does not return data. If the query returns data, an error is returned.
 *
 * @param cmd The query
 * @param mode Retry a failed query using the global query retry settings or not
 * @param errmsg_out Error output.
 * @return True on success, false on error or if query returned data
 */
bool MariaDBServer::execute_cmd_ex(const string& cmd, QueryRetryMode mode,
                                   std::string* errmsg_out, unsigned int* errno_out)
{
    auto conn = con;
    bool query_success = false;
    if (mode == QueryRetryMode::ENABLED)
    {
        query_success = (mxs_mysql_query(conn, cmd.c_str()) == 0);
    }
    else
    {
        query_success = (maxsql::mysql_query_ex(conn, cmd, 0, 0) == 0);
    }

    bool rval = false;
    if (query_success)
    {
        // In case query was a multiquery, loop for more resultsets. Error message is produced from first
        // non-empty resultset and does not specify the subquery.
        string results_errmsg;
        do
        {
            MYSQL_RES* result = mysql_store_result(conn);
            if (result)
            {
                int cols = mysql_num_fields(result);
                int rows = mysql_num_rows(result);
                if (results_errmsg.empty())
                {
                    results_errmsg = string_printf("Query '%s' on '%s' returned %d columns and %d rows "
                                                   "of data when none was expected.",
                                                   cmd.c_str(), name(), cols, rows);
                }
            }
        }
        while (mysql_next_result(conn) == 0);

        if (results_errmsg.empty())
        {
            rval = true;
        }
    }
    else
    {
        if (errmsg_out)
        {
            *errmsg_out = string_printf("Query '%s' failed on '%s': '%s' (%i).",
                                        cmd.c_str(), name(), mysql_error(conn), mysql_errno(conn));
        }
        if (errno_out)
        {
            *errno_out = mysql_errno(conn);
        }
    }
    return rval;
}

bool MariaDBServer::execute_cmd(const std::string& cmd, std::string* errmsg_out)
{
    return execute_cmd_ex(cmd, QueryRetryMode::ENABLED, errmsg_out);
}

bool MariaDBServer::execute_cmd_no_retry(const std::string& cmd,
                                         std::string* errmsg_out, unsigned int* errno_out)
{
    return execute_cmd_ex(cmd, QueryRetryMode::DISABLED, errmsg_out, errno_out);
}

/**
 * Execute a query which does not return data. If the query fails because of a network error
 * (e.g. Connector-C timeout), automatically retry the query until time is up. Uses max_statement_time
 * when available to ensure no lingering timed out commands are left on the server.
 *
 * @param cmd The query to execute. Should be a query with a predictable effect even when retried or
 * ran several times.
 * @param time_limit How long to retry. This does not overwrite the connector-c timeouts which are always
 * respected.
 * @param errmsg_out Error output
 * @return True, if successful.
 */
bool MariaDBServer::execute_cmd_time_limit(const std::string& cmd, maxbase::Duration time_limit,
                                           std::string* errmsg_out)
{
    StopWatch timer;
    string max_stmt_time;
    int connector_timeout = -1;
    if (m_capabilities.max_statement_time)
    {
        MXB_AT_DEBUG(int rv = ) mysql_get_optionv(con, MYSQL_OPT_READ_TIMEOUT,
                                                  &connector_timeout);
        mxb_assert(rv == 0);
        if (connector_timeout > 0)
        {
            max_stmt_time = string_printf("SET STATEMENT max_statement_time=%i FOR ", connector_timeout);
        }
    }

    const string command = max_stmt_time + cmd;
    // If a query lasts less than 1s, sleep so that at most 1 query/s is sent.
    // This prevents busy-looping when faced with some network errors.
    const Duration min_query_time {1s};

    // Even if time is up, try at least once.
    bool cmd_success = false;
    bool keep_trying = true;
    while (!cmd_success && keep_trying)
    {
        StopWatch query_timer;
        string error_msg;
        unsigned int errornum = 0;
        cmd_success = execute_cmd_no_retry(command, &error_msg, &errornum);
        auto query_time = query_timer.lap();

        // Check if there is time to retry.
        Duration time_remaining = time_limit - timer.split();
        bool non_fatal_connector_err = maxsql::mysql_is_net_error(errornum);
        keep_trying = (time_remaining.count() > 0)
            // Either a connector-c timeout or query was interrupted by max_statement_time.
            && (non_fatal_connector_err || (!max_stmt_time.empty() && errornum == ER_STATEMENT_TIMEOUT));

        if (!cmd_success)
        {
            if (keep_trying)
            {
                string retrying = string_printf("Retrying with %.1f seconds left.",
                                                mxb::to_secs(time_remaining));
                if (non_fatal_connector_err)
                {
                    MXS_WARNING("%s %s", error_msg.c_str(), retrying.c_str());
                }
                else
                {
                    // Timed out because of max_statement_time.
                    MXS_WARNING("Query '%s' timed out on '%s'. %s",
                                command.c_str(), name(), retrying.c_str());
                }

                if (query_time < min_query_time)
                {
                    Duration query_sleep = min_query_time - query_time;
                    Duration this_sleep = MXS_MIN(time_remaining, query_sleep);
                    std::this_thread::sleep_for(this_sleep);
                }
            }
            else if (errmsg_out)
            {
                *errmsg_out = error_msg;    // The error string already has all required info.
            }
        }
    }
    return cmd_success;
}

bool MariaDBServer::do_show_slave_status(string* errmsg_out)
{
    string query;
    bool all_slaves_status = false;
    if (m_capabilities.slave_status_all)
    {
        all_slaves_status = true;
        query = "SHOW ALL SLAVES STATUS;";
    }
    else if (m_capabilities.basic_support)
    {
        query = "SHOW SLAVE STATUS;";
    }
    else
    {
        mxb_assert(!true);      // This method should not be called for versions < 5.5
        return false;
    }

    auto result = execute_query(query, errmsg_out);
    if (!result)
    {
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
    auto i_seconds_behind_master = result->get_col_index("Seconds_Behind_Master");

    const char INVALID_DATA[] = "'%s' returned invalid data.";
    if (i_master_host < 0 || i_master_port < 0 || i_slave_io_running < 0 || i_slave_sql_running < 0
        || i_master_server_id < 0 || i_last_io_errno < 0 || i_last_io_error < 0 || i_last_sql_error < 0
        || i_seconds_behind_master < 0)
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
        if (i_connection_name < 0 || i_slave_rec_hbs < 0 || i_slave_hb_period < 0
            || i_using_gtid < 0 || i_gtid_io_pos < 0)
        {
            MXS_ERROR(INVALID_DATA, query.c_str());
            return false;
        }
    }

    SlaveStatusArray slave_status_new;
    bool parse_error = false;
    while (result->next_row())
    {
        SlaveStatus new_row(name());
        new_row.settings.master_endpoint = EndPoint(result->get_string(i_master_host),
                                                    result->get_int(i_master_port));

        new_row.last_io_error = result->get_string(i_last_io_error);
        new_row.last_sql_error = result->get_string(i_last_sql_error);

        new_row.slave_io_running =
            SlaveStatus::slave_io_from_string(result->get_string(i_slave_io_running));
        new_row.slave_sql_running = (result->get_string(i_slave_sql_running) == "Yes");
        new_row.master_server_id = result->get_int(i_master_server_id);

        // If slave connection is stopped, the value given by the backend is null.
        if (result->field_is_null(i_seconds_behind_master))
        {
            new_row.seconds_behind_master = mxs::Target::RLAG_UNDEFINED;
        }
        else
        {
            // Seconds_Behind_Master is actually uint64, but it will take a long time until it goes over
            // int64 limit.
            new_row.seconds_behind_master = result->get_int(i_seconds_behind_master);
        }

        if (all_slaves_status)
        {
            new_row.settings.name = result->get_string(i_connection_name);
            new_row.received_heartbeats = result->get_int(i_slave_rec_hbs);

            string using_gtid = result->get_string(i_using_gtid);
            string gtid_io_pos = result->get_string(i_gtid_io_pos);
            if (!gtid_io_pos.empty() && (using_gtid == "Current_Pos" || using_gtid == "Slave_Pos"))
            {
                new_row.gtid_io_pos = GtidList::from_string(gtid_io_pos);
            }
        }

        // If parsing fails, discard all query results.
        if (result->error())
        {
            parse_error = true;
            MXB_ERROR("Query '%s' returned invalid data: %s", query.c_str(), result->error_string().c_str());
            break;
        }

        // Before adding this row to the SlaveStatus array, compare the row to the one from the previous
        // monitor tick and fill in the last pieces of data.
        auto old_row = sstatus_find_previous_row(new_row, slave_status_new.size());
        if (old_row)
        {
            // When the new row was created, 'last_data_time' was set to the current time. If it seems
            // like the slave is not receiving data from the master, set the time to the one
            // in the previous monitor tick.
            if (new_row.received_heartbeats == old_row->received_heartbeats
                && new_row.gtid_io_pos == old_row->gtid_io_pos)
            {
                new_row.last_data_time = old_row->last_data_time;
            }

            // Copy master server pointer from old row. If this line is not reached because old row does
            // not exist, then the topology rebuild will set the master pointer.
            new_row.master_server = old_row->master_server;
        }

        // Finally, set the connection status.
        if (new_row.slave_io_running == SlaveStatus::SLAVE_IO_YES)
        {
            mxb_assert(new_row.master_server_id > 0);
            new_row.seen_connected = true;
        }
        else if (new_row.slave_io_running == SlaveStatus::SLAVE_IO_CONNECTING && old_row)
        {
            // Old connection data found. Even in this case the server id:s could be wrong if the
            // slave connection was cleared and remade between monitor loops.
            if (new_row.master_server_id == old_row->master_server_id && old_row->seen_connected)
            {
                new_row.seen_connected = true;
            }
        }

        // Row complete, add it to the array.
        slave_status_new.push_back(new_row);
    }

    if (!parse_error)
    {
        // Compare the previous array to the new one.
        if (!sstatus_array_topology_equal(slave_status_new))
        {
            m_topology_changed = true;
        }

        // Always write to m_slave_status. Even if the new status is equal by topology,
        // gtid:s etc may have changed.
        Guard guard(m_arraylock);
        m_slave_status = std::move(slave_status_new);
    }

    return !parse_error;
}

bool MariaDBServer::update_gtids(string* errmsg_out)
{
    static const string query = "SELECT @@gtid_current_pos, @@gtid_binlog_pos;";
    const int i_current_pos = 0;
    const int i_binlog_pos = 1;

    bool rval = false;
    auto result = execute_query(query, errmsg_out);
    if (result.get() != NULL)
    {
        Guard guard(m_arraylock);

        rval = true;
        if (result->next_row())
        {
            // Query returned at least some data.
            auto current_str = result->get_string(i_current_pos);
            auto binlog_str = result->get_string(i_binlog_pos);
            if (current_str.empty())
            {
                m_gtid_current_pos = GtidList();
            }
            else
            {
                m_gtid_current_pos = GtidList::from_string(current_str);

                std::vector<std::pair<uint32_t, uint64_t>> positions;

                for (auto d : m_gtid_current_pos.domains())
                {
                    positions.push_back({d, m_gtid_current_pos.get_gtid(d).m_sequence});
                }

                server->set_gtid_list(positions);
            }

            if (binlog_str.empty())
            {
                m_gtid_binlog_pos = GtidList();
            }
            else
            {
                m_gtid_binlog_pos = GtidList::from_string(binlog_str);
            }
        }
        else
        {
            // Query succeeded but returned 0 rows. This means that the server has no gtid:s.
            m_gtid_current_pos = GtidList();
            m_gtid_binlog_pos = GtidList();
        }
    }   // If query failed, do not update gtid:s.
    return rval;
}

bool MariaDBServer::update_replication_settings(std::string* errmsg_out)
{
    const string query = "SELECT @@gtid_strict_mode, @@log_bin, @@log_slave_updates;";
    bool rval = false;

    auto result = execute_query(query, errmsg_out);
    if (result.get() != NULL && result->next_row())
    {
        rval = true;
        m_rpl_settings.gtid_strict_mode = result->get_bool(0);
        m_rpl_settings.log_bin = result->get_bool(1);
        m_rpl_settings.log_slave_updates = result->get_bool(2);
    }
    return rval;
}

bool MariaDBServer::read_server_variables(string* errmsg_out)
{
    const string query_no_gtid = "SELECT @@global.server_id, @@read_only;";
    const string query_with_gtid = "SELECT @@global.server_id, @@read_only, @@global.gtid_domain_id;";
    const bool use_gtid = m_capabilities.gtid;
    const string& query = use_gtid ? query_with_gtid : query_no_gtid;

    int i_id = 0;
    int i_ro = 1;
    int i_domain = 2;
    bool rval = false;
    auto result = execute_query(query, errmsg_out);
    if (result != nullptr)
    {
        if (!result->next_row())
        {
            // This should not really happen, means that server is buggy.
            *errmsg_out = string_printf("Query '%s' did not return any rows.", query.c_str());
        }
        else
        {
            int64_t server_id_parsed = result->get_int(i_id);
            bool read_only_parsed = result->get_bool(i_ro);
            int64_t domain_id_parsed = GTID_DOMAIN_UNKNOWN;
            if (use_gtid)
            {
                domain_id_parsed = result->get_int(i_domain);
            }

            if (result->error())
            {
                // This is unlikely as well.
                *errmsg_out = string_printf("Query '%s' returned invalid data: %s",
                                            query.c_str(), result->error_string().c_str());
            }
            else
            {
                // All values parsed and within expected limits.
                rval = true;
                if (server_id_parsed != m_server_id)
                {
                    m_server_id = server_id_parsed;
                    m_topology_changed = true;
                }
                node_id = server_id_parsed;

                if (read_only_parsed != m_read_only)
                {
                    m_read_only = read_only_parsed;
                    m_topology_changed = true;
                }

                m_gtid_domain_id = domain_id_parsed;
            }
        }
    }
    return rval;
}

void MariaDBServer::warn_replication_settings() const
{
    const char* servername = name();
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

bool MariaDBServer::catchup_to_master(GeneralOpData& op, const GtidList& target)
{
    /* Prefer to use gtid_binlog_pos, as that is more reliable. But if log_slave_updates is not on,
     * use gtid_current_pos. */
    const bool use_binlog_pos = m_rpl_settings.log_bin && m_rpl_settings.log_slave_updates;
    bool time_is_up = false;    // Check at least once.
    bool gtid_reached = false;
    bool error = false;
    json_t** error_out = op.error_out;

    Duration sleep_time(200ms);     // How long to sleep before next iteration. Incremented slowly.
    StopWatch timer;

    while (!time_is_up && !gtid_reached && !error)
    {
        string error_msg;
        if (update_gtids(&error_msg))
        {
            const GtidList& compare_to = use_binlog_pos ? m_gtid_binlog_pos : m_gtid_current_pos;
            if (target.events_ahead(compare_to, GtidList::MISSING_DOMAIN_IGNORE) == 0)
            {
                gtid_reached = true;
            }
            else
            {
                // Query was successful but target gtid not yet reached. Check how much time left.
                op.time_remaining -= timer.lap();
                if (op.time_remaining.count() > 0)
                {
                    // Sleep for a moment, then try again.
                    Duration this_sleep = MXS_MIN(sleep_time, op.time_remaining);
                    std::this_thread::sleep_for(this_sleep);
                    sleep_time += 100ms;    // Sleep a bit more next iteration.
                }
                else
                {
                    time_is_up = true;
                }
            }
        }
        else
        {
            error = true;
            PRINT_MXS_JSON_ERROR(error_out, "Failed to update gtid on '%s' while waiting for catchup: %s",
                                 name(), error_msg.c_str());
        }
    }

    if (!error && !gtid_reached)
    {
        PRINT_MXS_JSON_ERROR(error_out, "Slave catchup timed out on slave '%s'.", name());
    }
    return gtid_reached;
}

bool MariaDBServer::binlog_on() const
{
    return m_rpl_settings.log_bin;
}

bool MariaDBServer::is_master() const
{
    return status_is_master(pending_status);
}

bool MariaDBServer::is_slave() const
{
    return status_is_slave(pending_status);
}

bool MariaDBServer::is_usable() const
{
    return status_is_usable(pending_status);
}

bool MariaDBServer::is_running() const
{
    return status_is_running(pending_status);
}

bool MariaDBServer::is_down() const
{
    return status_is_down(pending_status);
}

bool MariaDBServer::is_in_maintenance() const
{
    return status_is_in_maint(pending_status);
}

bool MariaDBServer::is_relay_master() const
{
    return status_is_relay(pending_status);
}

bool MariaDBServer::is_low_on_disk_space() const
{
    return status_is_disk_space_exhausted(pending_status);
}

bool MariaDBServer::has_status(uint64_t bits) const
{
    return (pending_status & bits) == bits;
}

bool MariaDBServer::had_status(uint64_t bits) const
{
    return (mon_prev_status & bits) == bits;
}

bool MariaDBServer::is_read_only() const
{
    return m_read_only;
}

const char* MariaDBServer::name() const
{
    return server->name();
}

json_t* MariaDBServer::to_json() const
{
    json_t* result = json_object();
    json_object_set_new(result, "name", json_string(name()));
    json_object_set_new(result, "server_id", json_integer(m_server_id));
    json_object_set_new(result, "read_only", json_boolean(m_read_only));

    Guard guard(m_arraylock);
    json_object_set_new(result,
                        "gtid_current_pos",
                        m_gtid_current_pos.empty() ? json_null() :
                        json_string(m_gtid_current_pos.to_string().c_str()));

    json_object_set_new(result,
                        "gtid_binlog_pos",
                        m_gtid_binlog_pos.empty() ? json_null() :
                        json_string(m_gtid_binlog_pos.to_string().c_str()));

    json_object_set_new(result,
                        "master_group",
                        (m_node.cycle == NodeData::CYCLE_NONE) ? json_null() : json_integer(m_node.cycle));

    auto lock = m_serverlock.status();
    json_object_set_new(result, "lock_held", (lock == ServerLock::Status::UNKNOWN) ? json_null() :
                        json_boolean(lock == ServerLock::Status::OWNED_SELF));

    json_t* slave_connections = json_array();
    for (const auto& sstatus : m_slave_status)
    {
        json_array_append_new(slave_connections, sstatus.to_json());
    }
    json_object_set_new(result, "slave_connections", slave_connections);
    return result;
}

bool MariaDBServer::can_replicate_from(MariaDBServer* master, string* reason_out)
{
    mxb_assert(reason_out);
    mxb_assert(is_usable());    // The server must be running.

    bool can_replicate = false;
    if (m_gtid_current_pos.empty())
    {
        *reason_out = string_printf("'%s' does not have a valid gtid_current_pos.", name());
    }
    else if (master->m_gtid_binlog_pos.empty())
    {
        *reason_out = string_printf("'%s' does not have a valid gtid_binlog_pos.", master->name());
    }
    else
    {
        can_replicate = m_gtid_current_pos.can_replicate_from(master->m_gtid_binlog_pos);
        if (!can_replicate)
        {
            *reason_out = string_printf("gtid_current_pos of '%s' (%s) is incompatible with "
                                        "gtid_binlog_pos of '%s' (%s).",
                                        name(), m_gtid_current_pos.to_string().c_str(),
                                        master->name(), master->m_gtid_binlog_pos.to_string().c_str());
        }
    }
    return can_replicate;
}

bool MariaDBServer::run_sql_from_file(const string& path, json_t** error_out)
{
    MYSQL* conn = con;
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
                PRINT_MXS_JSON_ERROR(error_out,
                                     "Error when reading sql text file '%s': '%s'.",
                                     path.c_str(),
                                     mxs_strerror(errno));
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
                    PRINT_MXS_JSON_ERROR(error_out,
                                         "Failed to execute sql from text file '%s'. Query: '%s'. "
                                         "Error: '%s'.",
                                         path.c_str(),
                                         line.c_str(),
                                         mysql_error(conn));
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

void MariaDBServer::monitor_server()
{
    string errmsg;
    /* Query different things depending on server version/type. */
    // TODO: Handle binlog router?
    bool query_ok = read_server_variables(&errmsg) && update_slave_status(&errmsg);
    if (query_ok && m_capabilities.gtid)
    {
        query_ok = update_gtids(&errmsg);
    }
    if (query_ok && m_settings.handle_event_scheduler && m_capabilities.events)
    {
        query_ok = update_enabled_events();
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
        MXS_WARNING("Error during monitor update of server '%s': %s", name(), errmsg.c_str());
        m_print_update_errormsg = false;
    }
}

/**
 * Update slave status of the server.
 *
 * @param errmsg_out Where to store an error message if query fails. Can be null.
 * @return True on success
 */
bool MariaDBServer::update_slave_status(string* errmsg_out)
{
    bool rval = do_show_slave_status(errmsg_out);
    if (rval)
    {
        /** Store master_id of current node. */
        master_id = !m_slave_status.empty() ?
            m_slave_status[0].master_server_id : SERVER_ID_UNKNOWN;
    }
    return rval;
}

void MariaDBServer::update_server_version()
{
    auto conn = con;
    auto srv = server;
    mxs_mysql_update_server_version(srv, conn);

    m_capabilities = Capabilities();
    auto& info = srv->info();
    auto type = info.type();

    if (type == ServerType::MARIADB || type == ServerType::MYSQL || type == ServerType::BLR)
    {
        /* Not a binlog server, check version number and supported features. */
        auto& srv_version = info.version_num();
        auto major = srv_version.major;
        auto minor = srv_version.minor;
        auto patch = srv_version.patch;
        // MariaDB/MySQL 5.5 is the oldest supported version. MySQL 6 and later are treated as 5.5.
        if ((major == 5 && minor >= 5) || major > 5)
        {
            m_capabilities.basic_support = true;
            // For more specific features, at least MariaDB 10.X is needed.
            if ((type == ServerType::MARIADB || type == ServerType::BLR)
                && major >= 10)
            {
                // 10.0.2 or 10.1.X or greater than 10
                if (((minor == 0 && patch >= 2) || minor >= 1) || major > 10)
                {
                    // Versions with gtid also support the extended slave status query.
                    m_capabilities.gtid = true;
                    m_capabilities.slave_status_all = true;
                    if (type != ServerType::BLR)
                    {
                        m_capabilities.events = true;
                    }
                }
                // 10.1.2 (10.1.1 has limited support, not enough) or 10.2.X or greater than 10
                if (((minor == 1 && patch >= 2) || minor >= 2) || major > 10)
                {
                    m_capabilities.max_statement_time = true;
                }
            }
        }
    }

    if (m_capabilities.basic_support)
    {
        if (!m_capabilities.gtid)
        {
            MXB_WARNING("Server '%s' (%s) does not support MariaDB gtid.", name(), info.version_string());
        }
    }
    else
    {
        MXB_ERROR("Server '%s' (%s) is unsupported. The server is ignored by the monitor.",
                  name(), info.version_string());
    }
}

void MariaDBServer::check_permissions()
{
    // Test with a typical query to make sure the monitor has sufficient permissions.
    const string query = "SHOW SLAVE STATUS;";
    string err_msg;
    auto result = execute_query(query, &err_msg);

    if (result.get() == NULL)
    {
        /* In theory, this could be due to other errors as well, but that is quite unlikely since the
         * connection was just checked. The end result is in any case that the server is not updated,
         * and that this test is retried next round. */
        set_status(SERVER_AUTH_ERROR);
        // Only print error if last round was ok.
        if (!had_status(SERVER_AUTH_ERROR))
        {
            MXS_WARNING("Error during monitor permissions test for server '%s': %s", name(), err_msg.c_str());
        }
    }
    else
    {
        clear_status(SERVER_AUTH_ERROR);
    }
}

void MariaDBServer::clear_status(uint64_t bits)
{
    clear_pending_status(bits);
}

void MariaDBServer::set_status(uint64_t bits)
{
    set_pending_status(bits);
}

/**
 * Compare if the given slave status array is equal to the one stored in the MariaDBServer.
 * Only compares the parts relevant for building replication topology: slave IO/SQL state,
 * host:port and master server id:s. When unsure, return false. This must match
 * 'build_replication_graph()' in the monitor class.
 *
 * @param new_slave_status Right hand side
 * @return True if equal
 */
bool MariaDBServer::sstatus_array_topology_equal(const SlaveStatusArray& new_slave_status)
{
    bool rval = true;
    const SlaveStatusArray& old_slave_status = m_slave_status;
    if (old_slave_status.size() != new_slave_status.size())
    {
        rval = false;
    }
    else
    {
        for (size_t i = 0; i < old_slave_status.size(); i++)
        {
            const auto new_row = new_slave_status[i];
            const auto old_row = old_slave_status[i];
            // Strictly speaking, the following should depend on the 'assume_unique_hostnames',
            // but the situations it would make a difference are so rare they can be ignored.
            if (new_row.slave_io_running != old_row.slave_io_running
                || new_row.slave_sql_running != old_row.slave_sql_running
                || new_row.settings.master_endpoint != old_row.settings.master_endpoint
                || new_row.settings.name != old_row.settings.name
                || new_row.master_server_id != old_row.master_server_id)
            {
                rval = false;
                break;
            }
        }
    }
    return rval;
}

/**
 * Check the slave status array stored in the MariaDBServer and find the row matching the connection in
 * 'search_row'.
 *
 * @param search_row What connection to search for
 * @param guess_ind Index where the search row could be located at. If incorrect, the array is searched.
 * @return The found row or NULL if not found
 */
const SlaveStatus* MariaDBServer::sstatus_find_previous_row(const SlaveStatus& search_row, size_t guess_ind)
{
    // Helper function. Checks if the connection in the new row is to the same server than in the old row.
    auto compare_rows = [](const SlaveStatus& lhs, const SlaveStatus& rhs) -> bool {
            return lhs.settings.name == rhs.settings.name
                   && lhs.settings.master_endpoint == rhs.settings.master_endpoint;
        };

    // Usually the same slave connection can be found from the same index than in the previous slave
    // status array, but this is not 100% (e.g. dba has just added a new connection).
    const SlaveStatus* rval = NULL;
    if (guess_ind < m_slave_status.size() && compare_rows(m_slave_status[guess_ind], search_row))
    {
        rval = &m_slave_status[guess_ind];
    }
    else
    {
        // The correct connection was not found where it should have been. Try looping.
        for (const SlaveStatus& old_row : m_slave_status)
        {
            if (compare_rows(old_row, search_row))
            {
                rval = &old_row;
                break;
            }
        }
    }
    return rval;
}

bool MariaDBServer::can_be_demoted_switchover(string* reason_out)
{
    bool demotable = false;
    string reason;
    string query_error;

    if (!is_usable())
    {
        reason = "it is not running or it is in maintenance.";
    }
    else if (!is_database())
    {
        reason = not_a_db;
    }
    else if (!update_replication_settings(&query_error))
    {
        reason = string_printf("it could not be queried: %s", query_error.c_str());
    }
    else if (!binlog_on())
    {
        reason = "its binary log is disabled.";
    }
    else if (!is_master() && !m_rpl_settings.log_slave_updates)
    {
        // This means that gtid_binlog_pos cannot be trusted.
        // TODO: reduce dependency on gtid_binlog_pos to get rid of this requirement
        reason = "it is not the master and log_slave_updates is disabled.";
    }
    else if (m_gtid_binlog_pos.empty())
    {
        reason = "it does not have a 'gtid_binlog_pos'.";
    }
    else
    {
        demotable = true;
    }

    if (!demotable && reason_out)
    {
        *reason_out = reason;
    }
    return demotable;
}

bool MariaDBServer::can_be_demoted_failover(FailoverType failover_mode, string* reason_out)
{
    bool demotable = false;
    string reason;

    if (is_master())
    {
        reason = "it is a running master.";
    }
    else if (is_running())
    {
        reason = "it is running.";
    }
    else if (failover_mode == FailoverType::SAFE && m_gtid_binlog_pos.empty())
    {
        reason = "it does not have a 'gtid_binlog_pos' and unsafe failover is disabled.";
    }
    else
    {
        demotable = true;
    }

    if (!demotable && reason_out)
    {
        *reason_out = reason;
    }
    return demotable;
}

bool MariaDBServer::can_be_promoted(OperationType op, const MariaDBServer* demotion_target,
                                    string* reason_out)
{
    bool promotable = false;
    string reason;
    string query_error;

    auto sstatus = slave_connection_status(demotion_target);
    if (is_master())
    {
        reason = "it is already the master.";
    }
    else if (!is_usable())
    {
        reason = "it is down or in maintenance.";
    }
    else if (!is_database())
    {
        reason = not_a_db;
    }
    else if (op == OperationType::SWITCHOVER && is_low_on_disk_space())
    {
        // Failover promotion with low disk space is allowed since it's better than nothing.
        reason = "it is low on disk space.";
    }
    else if (sstatus == NULL)
    {
        reason = string_printf("it is not replicating from '%s'.", demotion_target->name());
    }
    else if (sstatus->gtid_io_pos.empty())
    {
        reason = string_printf("its slave connection to '%s' is not using gtid.", demotion_target->name());
    }
    else if (op == OperationType::SWITCHOVER && sstatus->slave_io_running != SlaveStatus::SLAVE_IO_YES)
    {
        reason = string_printf("its slave connection to '%s' is broken.", demotion_target->name());
    }
    else if (!update_replication_settings(&query_error))
    {
        reason = string_printf("it could not be queried: %s", query_error.c_str());
    }
    else if (!binlog_on())
    {
        reason = "its binary log is disabled.";
    }
    else
    {
        promotable = true;
    }

    if (!promotable && reason_out)
    {
        *reason_out = reason;
    }
    return promotable;
}

const SlaveStatus* MariaDBServer::slave_connection_status(const MariaDBServer* target) const
{
    mxb_assert(target);
    // The slave node may have several slave connections, need to find the one that is
    // connected to the parent. Most of this has already been done in 'build_replication_graph'.
    const SlaveStatus* rval = NULL;
    for (const SlaveStatus& ss : m_slave_status)
    {
        if (ss.master_server == target)
        {
            rval = &ss;
            break;
        }
    }
    return rval;
}

const SlaveStatus* MariaDBServer::slave_connection_status_host_port(const MariaDBServer* target) const
{
    EndPoint target_endpoint(target->server);
    for (const SlaveStatus& ss : m_slave_status)
    {
        if (ss.settings.master_endpoint == target_endpoint)
        {
            return &ss;
        }
    }
    return NULL;
}

bool MariaDBServer::enable_events(BinlogMode binlog_mode, const EventNameSet& event_names, json_t** error_out)
{
    EventStatusMapper mapper = [&event_names](const EventInfo& event) {
            string rval;
            if (event_names.count(event.name) > 0
                && (event.status == "SLAVESIDE_DISABLED" || event.status == "DISABLED"))
            {
                rval = "ENABLE";
            }
            return rval;
        };
    return alter_events(binlog_mode, mapper, error_out);
}

bool MariaDBServer::disable_events(BinlogMode binlog_mode, json_t** error_out)
{
    EventStatusMapper mapper = [](const EventInfo& event) {
            string rval;
            if (event.status == "ENABLED")
            {
                rval = "DISABLE ON SLAVE";
            }
            return rval;
        };
    return alter_events(binlog_mode, mapper, error_out);
}

/**
 * Alter scheduled server events.
 *
 * @param binlog_mode Should binary logging be disabled while performing this task.
 * @param mapper A function which takes an event and returns the requested event state. If empty is returned,
 * event is not altered.
 * @param error_out Error output
 * @return True if all requested alterations succeeded.
 */
bool
MariaDBServer::alter_events(BinlogMode binlog_mode, const EventStatusMapper& mapper, json_t** error_out)
{
    // If the server is rejoining the cluster, no events may be added to binlog. The ALTER EVENT query
    // itself adds events. To prevent this, disable the binlog for this method.
    string error_msg;
    const bool disable_binlog = (binlog_mode == BinlogMode::BINLOG_OFF);
    if (disable_binlog)
    {
        if (!execute_cmd("SET @@session.sql_log_bin=0;", &error_msg))
        {
            const char FMT[] = "Could not disable session binlog on '%s': %s Server events not disabled.";
            PRINT_MXS_JSON_ERROR(error_out, FMT, name(), error_msg.c_str());
            return false;
        }
    }

    int target_events = 0;
    int events_altered = 0;
    // Helper function which alters an event depending on the mapper-function.
    EventManipulator alterer = [this, &target_events, &events_altered, &mapper](const EventInfo& event,
                                                                                json_t** error_out) {
            string target_state = mapper(event);
            if (!target_state.empty())
            {
                target_events++;
                if (alter_event(event, target_state, error_out))
                {
                    events_altered++;
                }
            }
        };

    bool rval = false;
    // TODO: For better error handling, this function should try to re-enable any disabled events if a later
    // disable fails.
    if (events_foreach(alterer, error_out))
    {
        if (target_events > 0)
        {
            // Reset character set and collation.
            string charset_errmsg;
            if (!execute_cmd("SET NAMES latin1 COLLATE latin1_swedish_ci;", &charset_errmsg))
            {
                MXS_ERROR("Could not reset character set: %s", charset_errmsg.c_str());
            }
            warn_event_scheduler();
        }
        if (target_events == events_altered)
        {
            rval = true;
        }
    }

    if (disable_binlog)
    {
        // Failure in re-enabling the session binlog doesn't really matter because we don't want the monitor
        // generating binlog events anyway.
        execute_cmd("SET @@session.sql_log_bin=1;");
    }
    return rval;
}

/**
 * Print a warning if the event scheduler is off.
 */
void MariaDBServer::warn_event_scheduler()
{
    string error_msg;
    const string scheduler_query = "SELECT * FROM information_schema.PROCESSLIST "
                                   "WHERE User = 'event_scheduler' AND Command = 'Daemon';";
    auto proc_list = execute_query(scheduler_query, &error_msg);
    if (proc_list.get() == NULL)
    {
        MXS_ERROR("Could not query the event scheduler status of '%s': %s", name(), error_msg.c_str());
    }
    else
    {
        if (proc_list->get_row_count() < 1)
        {
            // This is ok, though unexpected since events were found.
            MXS_WARNING("Event scheduler is inactive on '%s' although events were found.", name());
        }
    }
}

/**
 * Run the manipulator function on every server event.
 *
 * @param func The manipulator function
 * @param error_out Error output
 * @return True if event information could be read from information_schema.EVENTS. The return value does not
 * depend on the manipulator function.
 */
bool MariaDBServer::events_foreach(EventManipulator& func, json_t** error_out)
{
    string error_msg;
    // Get info about all scheduled events on the server.
    auto event_info = execute_query("SELECT * FROM information_schema.EVENTS;", &error_msg);
    if (event_info.get() == NULL)
    {
        MXS_ERROR("Could not query event status of '%s': %s Event handling can be disabled by "
                  "setting '%s' to false.",
                  name(), error_msg.c_str(), CN_HANDLE_EVENTS);
        return false;
    }

    auto db_name_ind = event_info->get_col_index("EVENT_SCHEMA");
    auto event_name_ind = event_info->get_col_index("EVENT_NAME");
    auto event_definer_ind = event_info->get_col_index("DEFINER");
    auto event_status_ind = event_info->get_col_index("STATUS");
    auto charset_ind = event_info->get_col_index("CHARACTER_SET_CLIENT");
    auto collation_ind = event_info->get_col_index("COLLATION_CONNECTION");
    mxb_assert(db_name_ind > 0 && event_name_ind > 0 && event_definer_ind > 0 && event_status_ind > 0
               && charset_ind > 0 && collation_ind > 0);

    while (event_info->next_row())
    {
        EventInfo event;
        event.name = event_info->get_string(db_name_ind) + "." + event_info->get_string(event_name_ind);
        event.definer = event_info->get_string(event_definer_ind);
        event.status = event_info->get_string(event_status_ind);
        event.charset = event_info->get_string(charset_ind);
        event.collation = event_info->get_string(collation_ind);
        func(event, error_out);
    }
    return true;
}

/**
 * Alter a scheduled server event, setting its status.
 *
 * @param event Event to alter
 * @param target_status Status to set
 * @param error_out Error output
 * @return True if status was set
 */
bool MariaDBServer::alter_event(const EventInfo& event, const string& target_status, json_t** error_out)
{
    bool rval = false;
    string error_msg;
    // An ALTER EVENT by default changes the definer (owner) of the event to the monitor user.
    // This causes problems if the monitor user does not have privileges to run
    // the event contents. Prevent this by setting definer explicitly.
    // The definer may be of the form user@host. If host includes %, then it must be quoted.
    // For simplicity, quote the host always.
    string quoted_definer;
    auto loc_at = event.definer.find('@');
    if (loc_at != string::npos)
    {
        auto host_begin = loc_at + 1;
        quoted_definer = event.definer.substr(0, loc_at + 1)
            +   // host_begin may be the null-char if @ was the last char
            "'" + event.definer.substr(host_begin, string::npos) + "'";
    }
    else
    {
        // Just the username
        quoted_definer = event.definer;
    }

    // Change character set and collation to the values in the event description. Otherwise, the event
    // values could be changed to whatever the monitor connection happens to be using.
    string alter_event_query = string_printf(
        "SET NAMES %s COLLATE %s; ALTER DEFINER = %s EVENT %s %s;",
        event.charset.c_str(), event.collation.c_str(), quoted_definer.c_str(), event.name.c_str(),
        target_status.c_str());

    if (execute_cmd(alter_event_query, &error_msg))
    {
        rval = true;
        const char FMT[] = "Event '%s' on server '%s' set to '%s'.";
        MXS_NOTICE(FMT, event.name.c_str(), name(), target_status.c_str());
    }
    else
    {
        const char FMT[] = "Could not alter event '%s' on server '%s': %s";
        PRINT_MXS_JSON_ERROR(error_out, FMT, event.name.c_str(), name(), error_msg.c_str());
    }
    return rval;
}

bool MariaDBServer::reset_all_slave_conns(json_t** error_out)
{
    string error_msg;
    bool error = false;
    for (const auto& slave_conn : m_slave_status)
    {
        auto conn_name = slave_conn.settings.name;
        auto stop = string_printf("STOP SLAVE '%s';", conn_name.c_str());
        auto reset = string_printf("RESET SLAVE '%s' ALL;", conn_name.c_str());
        if (!execute_cmd(stop, &error_msg) || !execute_cmd(reset, &error_msg))
        {
            error = true;
            string log_message = conn_name.empty() ?
                string_printf("Error when reseting the default slave connection of '%s': %s",
                              name(), error_msg.c_str()) :
                string_printf("Error when reseting the slave connection '%s' of '%s': %s",
                              conn_name.c_str(), name(), error_msg.c_str());
            PRINT_MXS_JSON_ERROR(error_out, "%s", log_message.c_str());
            break;
        }
    }

    if (!error && !m_slave_status.empty())
    {
        MXS_NOTICE("Removed %lu slave connection(s) from '%s'.", m_slave_status.size(), name());
    }
    return !error;
}

bool MariaDBServer::promote(GeneralOpData& general, ServerOperation& promotion, OperationType type,
                            const MariaDBServer* demotion_target)
{
    mxb_assert(type == OperationType::SWITCHOVER || type == OperationType::FAILOVER
               || type == OperationType::UNDO_DEMOTION);
    json_t** const error_out = general.error_out;

    StopWatch timer;
    bool stopped = false;
    if (type == OperationType::SWITCHOVER || type == OperationType::FAILOVER)
    {
        // In normal circumstances, this should only be called for a master-slave pair.
        auto master_conn = slave_connection_status(demotion_target);
        mxb_assert(master_conn);
        if (master_conn == NULL)
        {
            PRINT_MXS_JSON_ERROR(error_out,
                                 "'%s' is not a slave of '%s' and cannot be promoted to its place.",
                                 name(), demotion_target->name());
            return false;
        }

        // Step 1: Stop & reset slave connections. If doing a failover, only remove the connection to demotion
        // target. In case of switchover, remove other slave connections as well since the demotion target
        // will take them over.
        if (type == OperationType::SWITCHOVER)
        {
            stopped = remove_slave_conns(general, m_slave_status);
        }
        else if (type == OperationType::FAILOVER)
        {
            stopped = remove_slave_conns(general, {*master_conn});
        }
    }

    bool success = false;
    if (stopped || type == OperationType::UNDO_DEMOTION)
    {
        // Step 2: If demotion target is master, meaning this server will become the master,
        // enable writing and scheduled events. Also, run promotion_sql_file.
        bool promotion_error = false;
        if (promotion.to_from_master)
        {
            // Disabling read-only should be quick.
            bool ro_disabled = set_read_only(ReadOnlySetting::DISABLE, general.time_remaining, error_out);
            general.time_remaining -= timer.restart();
            if (!ro_disabled)
            {
                promotion_error = true;
            }
            else
            {
                if (m_settings.handle_event_scheduler)
                {
                    // TODO: Add query replying to enable_events
                    bool events_enabled = enable_events(BinlogMode::BINLOG_OFF, promotion.events_to_enable,
                                                        error_out);
                    general.time_remaining -= timer.restart();
                    if (!events_enabled)
                    {
                        promotion_error = true;
                        PRINT_MXS_JSON_ERROR(error_out, "Failed to enable events on '%s'.", name());
                    }
                }

                // Run promotion_sql_file if no errors so far.
                const string& sql_file = m_settings.promotion_sql_file;
                if (!promotion_error && !sql_file.empty())
                {
                    bool file_ran_ok = run_sql_from_file(sql_file, error_out);
                    general.time_remaining -= timer.restart();
                    if (!file_ran_ok)
                    {
                        promotion_error = true;
                        PRINT_MXS_JSON_ERROR(error_out,
                                             "Execution of file '%s' failed during promotion of server '%s'.",
                                             sql_file.c_str(), name());
                    }
                }
            }
        }

        // Step 3: Copy or merge slave connections from demotion target. The logic used depends on the
        // operation.
        if (!promotion_error)
        {
            if (type == OperationType::SWITCHOVER)
            {
                if (copy_slave_conns(general, promotion.conns_to_copy, demotion_target))
                {
                    success = true;
                }
                else
                {
                    PRINT_MXS_JSON_ERROR(error_out, "Could not copy slave connections from '%s' to '%s'.",
                                         demotion_target->name(), name());
                }
            }
            else if (type == OperationType::FAILOVER)
            {
                if (merge_slave_conns(general, promotion.conns_to_copy))
                {
                    success = true;
                }
                else
                {
                    PRINT_MXS_JSON_ERROR(error_out, "Could not merge slave connections from '%s' to '%s'.",
                                         demotion_target->name(), name());
                }
            }
            else if (type == OperationType::UNDO_DEMOTION)
            {
                if (copy_slave_conns(general, promotion.conns_to_copy, nullptr))
                {
                    success = true;
                }
                else
                {
                    PRINT_MXS_JSON_ERROR(error_out, "Could not restore slave connections of '%s' when "
                                                    "reversing demotion.", name());
                }
            }
        }
    }
    return success;
}

bool MariaDBServer::demote(GeneralOpData& general, ServerOperation& demotion, OperationType type)
{
    mxb_assert(demotion.target == this);
    mxb_assert(type == OperationType::SWITCHOVER || type == OperationType::REJOIN);
    json_t** const error_out = general.error_out;
    bool success = false;

    // Step 1: Stop & reset slave connections. The promotion target will copy them. The connection
    // information has been backed up in the operation object.
    if (remove_slave_conns(general, m_slave_status))
    {
        // Step 2: If this server is master, disable writes and scheduled events, flush logs,
        // update gtid:s, run demotion_sql_file.

        // In theory, this part should be ran in the opposite order so it would "reverse"
        // the promotion code. However, it's probably better to run the most
        // likely part to fail, setting read_only=1, first to make undoing easier. Setting
        // read_only may fail if another session has table locks or is doing long writes.
        bool demotion_error = false;
        if (demotion.to_from_master)
        {
            // The server should either be the master or be a standalone being rejoined.
            mxb_assert(is_master() || m_slave_status.empty());

            // Step 2a: Remove [Master] from this server. This prevents compatible routers (RWS)
            // from routing writes to this server. Writes in flight will go through, at least until
            // read_only is set.
            this->server->clear_status(SERVER_MASTER);

            // Step 2b: If other users with SUPER privileges are on, kick them out now since
            // read_only doesn't stop them from doing writes. This does not stop them from immediately
            // logging back in but it's better than nothing. This also stops super-user writes going
            // through MaxScale.
            if (type == OperationType::SWITCHOVER && !kick_out_super_users(general))
            {
                demotion_error = true;
            }

            // Step 2c: Enabling read-only can take time if writes are on or table locks taken.
            StopWatch timer;
            if (!demotion_error)
            {
                bool ro_enabled = set_read_only(ReadOnlySetting::ENABLE, general.time_remaining, error_out);
                general.time_remaining -= timer.lap();
                if (!ro_enabled)
                {
                    demotion_error = true;
                }
            }

            if (!demotion_error && m_settings.handle_event_scheduler)
            {
                // TODO: Add query replying to enable_events
                // Step 2b: Using BINLOG_OFF to avoid adding any gtid events,
                // which could break external replication.
                bool events_disabled = disable_events(BinlogMode::BINLOG_OFF, error_out);
                general.time_remaining -= timer.lap();
                if (!events_disabled)
                {
                    demotion_error = true;
                    PRINT_MXS_JSON_ERROR(error_out, "Failed to disable events on '%s'.", name());
                }
            }

            // Step 2e: Run demotion_sql_file if no errors so far.
            const string& sql_file = m_settings.demotion_sql_file;
            if (!demotion_error && !sql_file.empty())
            {
                bool file_ran_ok = run_sql_from_file(sql_file, error_out);
                general.time_remaining -= timer.lap();
                if (!file_ran_ok)
                {
                    demotion_error = true;
                    PRINT_MXS_JSON_ERROR(error_out,
                                         "Execution of file '%s' failed during demotion of server '%s'.",
                                         sql_file.c_str(), name());
                }
            }

            if (!demotion_error)
            {
                // Step 2f: FLUSH LOGS to ensure that all events have been written to binlog.
                string error_msg;
                bool logs_flushed = execute_cmd_time_limit("FLUSH LOGS;", general.time_remaining,
                                                           &error_msg);
                general.time_remaining -= timer.lap();
                if (!logs_flushed)
                {
                    demotion_error = true;
                    PRINT_MXS_JSON_ERROR(error_out,
                                         "Failed to flush binary logs of '%s' during demotion: %s.",
                                         name(), error_msg.c_str());
                }
            }
        }

        if (!demotion_error)
        {
            // Finally, update gtid:s.
            string error_msg;
            if (update_gtids(&error_msg))
            {
                success = true;
            }
            else
            {
                demotion_error = true;
                PRINT_MXS_JSON_ERROR(error_out, "Failed to update gtid:s of '%s' during demotion: %s.",
                                     name(), error_msg.c_str());
            }
        }

        if (demotion_error && demotion.to_from_master)
        {
            // Read_only was enabled (or tried to be enabled) but a later step failed.
            // Disable read_only. Connection is likely broken so use a short time limit.
            // Even this is insufficient, because the server may still be executing the old
            // 'SET GLOBAL read_only=1' query.
            // TODO: add smarter undo, KILL QUERY etc.
            set_read_only(ReadOnlySetting::DISABLE, 0s, NULL);
        }
    }
    return success;
}

/**
 * Stop and optionally reset/reset-all a slave connection.
 *
 * @param conn_name Slave connection name. Use empty string for the nameless connection.
 * @param mode STOP, RESET or RESET ALL
 * @param time_limit Operation time limit
 * @param error_out Error output
 * @return True on success
 */
bool MariaDBServer::stop_slave_conn(const std::string& conn_name, StopMode mode, Duration time_limit,
                                    json_t** error_out)
{
    /* STOP SLAVE is a bit problematic, since sometimes it seems to take several seconds to complete.
     * If this time is greater than the connection read timeout, connector-c will cut the connection/
     * query. The query is likely completed afterwards by the server. To prevent false errors,
     * try the query repeatedly until time is up. Fortunately, the server doesn't consider stopping
     * an already stopped slave connection an error. */
    Duration time_left = time_limit;
    StopWatch timer;
    string stop = string_printf("STOP SLAVE '%s';", conn_name.c_str());
    string error_msg;
    bool stop_success = execute_cmd_time_limit(stop, time_left, &error_msg);
    time_left -= timer.restart();

    bool rval = false;
    if (stop_success)
    {
        // The RESET SLAVE-query can also take a while if there is lots of relay log to delete.
        // Very rare, though.
        if (mode == StopMode::RESET || mode == StopMode::RESET_ALL)
        {
            string reset = string_printf("RESET SLAVE '%s'%s;",
                                         conn_name.c_str(), (mode == StopMode::RESET_ALL) ? " ALL" : "");
            if (execute_cmd_time_limit(reset, time_left, &error_msg))
            {
                rval = true;
            }
            else
            {
                PRINT_MXS_JSON_ERROR(error_out, "Failed to reset slave connection on '%s': %s",
                                     name(), error_msg.c_str());
            }
        }
        else
        {
            rval = true;
        }
    }
    else
    {
        PRINT_MXS_JSON_ERROR(error_out, "Failed to stop slave connection on '%s': %s",
                             name(), error_msg.c_str());
    }
    return rval;
}

/**
 * Removes the given slave connections from the server and then updates slave connection status.
 * The slave connections of the server object will change during this method, so any pointers and
 * references to such may be invalidated and should be re-acquired.
 *
 * @param op Operation descriptor
 * @param conns_to_remove Which connections should be removed
 * @return True if successful
 */
bool MariaDBServer::remove_slave_conns(GeneralOpData& op, const SlaveStatusArray& conns_to_remove)
{
    json_t** error_out = op.error_out;
    maxbase::Duration& time_remaining = op.time_remaining;
    StopWatch timer;
    // Take a backup of the soon to be removed connections so they can be compared properly after an update.
    SlaveStatusArray conns_to_remove_copy = conns_to_remove;

    bool stop_slave_error = false;
    for (size_t i = 0; !stop_slave_error && i < conns_to_remove.size(); i++)
    {
        if (!stop_slave_conn(conns_to_remove[i].settings.name, StopMode::RESET_ALL, time_remaining,
                             error_out))
        {
            stop_slave_error = true;
        }
        time_remaining -= timer.lap();
    }

    bool success = false;
    if (stop_slave_error)
    {
        PRINT_MXS_JSON_ERROR(error_out, "Failed to remove slave connection(s) from '%s'.", name());
    }
    else
    {
        // Check that the slave connections are really gone by comparing connection names. It's probably
        // enough to just update the slave status. Checking that the connections are really gone is
        // likely overkill, but doesn't hurt.
        string error_msg;
        if (do_show_slave_status(&error_msg))
        {
            // Insert all existing connection names to a set, then check that none of the removed ones are
            // there.
            std::set<string> connection_names;
            for (auto& slave_conn : m_slave_status)
            {
                connection_names.insert(slave_conn.settings.name);
            }
            int found = 0;
            for (auto& removed_conn : conns_to_remove_copy)
            {
                if (connection_names.count(removed_conn.settings.name) > 0)
                {
                    found++;
                }
            }

            if (found == 0)
            {
                success = true;
            }
            else
            {
                // This means server is really bugging.
                PRINT_MXS_JSON_ERROR(error_out,
                                     "'%s' still has %i removed slave connections, "
                                     "RESET SLAVE must have failed.", name(), found);
            }
        }
        else
        {
            PRINT_MXS_JSON_ERROR(error_out, "Failed to update slave connections of '%s': %s",
                                 name(), error_msg.c_str());
        }
    }
    time_remaining -= timer.lap();
    return success;
}

bool MariaDBServer::set_read_only(ReadOnlySetting setting, maxbase::Duration time_limit, json_t** error_out)
{
    int new_val = (setting == ReadOnlySetting::ENABLE) ? 1 : 0;
    string cmd = string_printf("SET GLOBAL read_only=%i;", new_val);
    string error_msg;
    bool success = execute_cmd_time_limit(cmd, time_limit, &error_msg);
    if (!success)
    {
        string target_str = (setting == ReadOnlySetting::ENABLE) ? "enable" : "disable";
        PRINT_MXS_JSON_ERROR(error_out,
                             "Failed to %s read_only on '%s': %s",
                             target_str.c_str(), name(), error_msg.c_str());
    }
    return success;
}

/**
 * Merge slave connections to this server (promotion target). This should only
 * be used during failover promotion.
 *
 * @param op Operation descriptor
 * @param conns_to_merge Connections which should be merged
 * @return True on success
 */
bool MariaDBServer::merge_slave_conns(GeneralOpData& op, const SlaveStatusArray& conns_to_merge)
{
    /* When promoting a server during failover, the situation is more complicated than in switchover.
     * Connections cannot be moved to the demotion target (= failed server) as it is off. This means
     * that the promoting server must combine the roles of both itself and the failed server. Only the
     * slave connection replicating from the failed server has been removed. This means that
     * the promotion and demotion targets may have identical connections (connections going to
     * the same server id or the same host:port). These connections should not be copied or modified.
     * It's possible that the master had different settings for a duplicate slave connection,
     * in this case the settings on the master are lost.
     * TODO: think if the master's settings should take priority.
     * Also, connection names may collide between the two servers, in this case try to generate
     * a simple name for the new connection. */

    // Helper function for checking if a slave connection should be ignored.
    auto conn_can_be_merged = [this](const SlaveStatus& slave_conn, string* ignore_reason_out) -> bool {
            bool accepted = true;
            auto master_id = slave_conn.master_server_id;
            EndPoint my_host_port(server);
            // The connection is only merged if it satisfies the copy-conditions. Merging has also
            // additional requirements.
            string ignore_reason;
            if (!slave_conn.should_be_copied(&ignore_reason))
            {
                accepted = false;
            }
            else if (master_id == m_server_id)
            {
                // This is not an error but indicates a complicated topology. In any case, ignore this.
                accepted = false;
                ignore_reason = string_printf("it points to '%s' (according to server id:s).", name());
            }
            else if (slave_conn.settings.master_endpoint == my_host_port)
            {
                accepted = false;
                ignore_reason = string_printf("it points to '%s' (according to master host:port).", name());
            }
            else
            {
                // Compare to connections already existing on this server.
                for (const SlaveStatus& my_slave_conn : m_slave_status)
                {
                    if (my_slave_conn.seen_connected && my_slave_conn.master_server_id == master_id)
                    {
                        accepted = false;
                        const char format[] = "its Master_Server_Id (%" PRIi64
                            ") matches an existing slave connection on '%s'.";
                        ignore_reason = string_printf(format, master_id, name());
                    }
                    else if (my_slave_conn.settings.master_endpoint == slave_conn.settings.master_endpoint)
                    {
                        accepted = false;
                        const auto& endpoint = slave_conn.settings.master_endpoint;
                        ignore_reason = string_printf(
                            "its Master_Host (%s) and Master_Port (%i) match an existing "
                            "slave connection on %s.",
                            endpoint.host().c_str(), endpoint.port(), name());
                    }
                }
            }

            if (!accepted)
            {
                *ignore_reason_out = ignore_reason;
            }
            return accepted;
        };

    // Need to keep track of connection names (both existing and new) to avoid using an existing name.
    std::set<string> connection_names;
    for (const auto& conn : m_slave_status)
    {
        connection_names.insert(conn.settings.name);
    }

    // Helper function which checks that a connection name is unique and modifies it if not.
    auto check_modify_conn_name = [this, &connection_names](SlaveStatus::Settings* conn_settings) -> bool {
            bool name_is_unique = false;
            string conn_name = conn_settings->name;
            if (connection_names.count(conn_name) > 0)
            {
                // If the name is used, generate a name using the host:port of the master,
                // it should be unique.
                string second_try = "To " + conn_settings->master_endpoint.to_string();
                if (connection_names.count(second_try) > 0)
                {
                    // Even this one exists, something is really wrong. Give up.
                    MXS_ERROR("Could not generate a unique connection name for '%s': both '%s' and '%s' are "
                              "already taken.", name(), conn_name.c_str(), second_try.c_str());
                }
                else
                {
                    MXS_WARNING("A slave connection with name '%s' already exists on '%s', using generated "
                                "name '%s' instead.", conn_name.c_str(), name(), second_try.c_str());
                    conn_settings->name = second_try;
                    name_is_unique = true;
                }
            }
            else
            {
                name_is_unique = true;
            }
            return name_is_unique;
        };

    bool error = false;
    for (size_t i = 0; !error && (i < conns_to_merge.size()); i++)
    {
        // Need a copy of the array element here since it may be modified.
        SlaveStatus slave_conn = conns_to_merge[i];
        string ignore_reason;
        if (conn_can_be_merged(slave_conn, &ignore_reason))
        {
            auto& conn_settings = slave_conn.settings;
            if (check_modify_conn_name(&conn_settings))
            {
                if (create_start_slave(op, conn_settings))
                {
                    connection_names.insert(conn_settings.name);
                }
                else
                {
                    error = true;
                }
            }
            else
            {
                error = true;
            }
        }
        else
        {
            mxb_assert(!ignore_reason.empty());
            MXS_WARNING("%s was ignored when promoting '%s' because %s",
                        slave_conn.settings.to_string().c_str(), name(), ignore_reason.c_str());
        }
    }

    return !error;
}

bool MariaDBServer::copy_slave_conns(GeneralOpData& op, const SlaveStatusArray& conns_to_copy,
                                     const MariaDBServer* replacement)
{
    mxb_assert(m_slave_status.empty());
    bool start_slave_error = false;
    for (size_t i = 0; i < conns_to_copy.size() && !start_slave_error; i++)
    {
        SlaveStatus slave_conn = conns_to_copy[i];      // slave_conn may be modified
        string reason_not_copied;
        if (slave_conn.should_be_copied(&reason_not_copied))
        {
            // Any slave connection that was going to this server itself is instead directed
            // to the replacement server.
            bool ok_to_copy = true;
            if (slave_conn.master_server_id == m_server_id)
            {
                if (replacement)
                {
                    slave_conn.settings.master_endpoint = EndPoint(replacement->server);
                }
                else
                {
                    // This is only possible if replication is configured wrong and we are
                    // undoing a switchover demotion.
                    ok_to_copy = false;
                    MXB_WARNING("Server id:s of '%s' and %s are identical, not copying the connection "
                                "to '%s'.",
                                name(), slave_conn.settings.master_endpoint.to_string().c_str(), name());
                }
            }

            if (ok_to_copy && !create_start_slave(op, slave_conn.settings))
            {
                start_slave_error = true;
            }
        }
        else
        {
            MXS_WARNING("%s was not copied to '%s' because %s",
                        slave_conn.settings.to_string().c_str(), name(), reason_not_copied.c_str());
        }
    }
    return !start_slave_error;
}

bool MariaDBServer::create_start_slave(GeneralOpData& op, const SlaveStatus::Settings& conn_settings)
{
    maxbase::Duration& time_remaining = op.time_remaining;
    StopWatch timer;
    string error_msg;
    bool success = false;

    SlaveStatus::Settings new_settings(conn_settings.name, conn_settings.master_endpoint, name());
    string change_master = generate_change_master_cmd(new_settings);
    bool conn_created = execute_cmd_time_limit(change_master, time_remaining, &error_msg);
    time_remaining -= timer.restart();
    if (conn_created)
    {
        string start_slave = string_printf("START SLAVE '%s';", new_settings.name.c_str());
        bool slave_started = execute_cmd_time_limit(start_slave, time_remaining, &error_msg);
        time_remaining -= timer.restart();
        if (slave_started)
        {
            success = true;
            MXS_NOTICE("%s created and started.", new_settings.to_string().c_str());
        }
        else
        {
            MXS_ERROR("%s could not be started: %s",
                      new_settings.to_string().c_str(), error_msg.c_str());
        }
    }
    else
    {
        // TODO: This may currently print out passwords.
        MXS_ERROR("%s could not be created: %s",
                  new_settings.to_string().c_str(), error_msg.c_str());
    }
    return success;
}

/**
 * Generate a CHANGE MASTER TO-query.
 *
 * @param conn_settings Existing slave connection settings to emulate
 * @return Generated query
 */
string MariaDBServer::generate_change_master_cmd(const SlaveStatus::Settings& conn_settings)
{
    string change_cmd;
    change_cmd += string_printf("CHANGE MASTER '%s' TO MASTER_HOST = '%s', MASTER_PORT = %i, ",
                                conn_settings.name.c_str(),
                                conn_settings.master_endpoint.host().c_str(),
                                conn_settings.master_endpoint.port());
    change_cmd += "MASTER_USE_GTID = current_pos, ";
    if (m_settings.replication_ssl)
    {
        change_cmd += "MASTER_SSL = 1, ";
    }
    change_cmd += string_printf("MASTER_USER = '%s', ", m_settings.replication_user.c_str());
    const char MASTER_PW[] = "MASTER_PASSWORD = '%s';";
#if defined (SS_DEBUG)
    string change_cmd_nopw = change_cmd;
    change_cmd_nopw += string_printf(MASTER_PW, "******");
    MXS_DEBUG("Change master command is '%s'.", change_cmd_nopw.c_str());
#endif
    change_cmd += string_printf(MASTER_PW, m_settings.replication_password.c_str());
    return change_cmd;
}

bool
MariaDBServer::redirect_existing_slave_conn(GeneralOpData& op, const SlaveStatus::Settings& conn_settings,
                                            const MariaDBServer* new_master)
{
    auto error_out = op.error_out;
    maxbase::Duration& time_remaining = op.time_remaining;
    StopWatch timer;
    bool success = false;

    // First, just stop the slave connection.
    string conn_name = conn_settings.name;
    bool stopped = stop_slave_conn(conn_name, StopMode::STOP_ONLY, time_remaining, error_out);
    time_remaining -= timer.restart();
    if (stopped)
    {
        SlaveStatus::Settings modified_settings = conn_settings;
        modified_settings.master_endpoint = EndPoint(new_master->server);
        string change_master = generate_change_master_cmd(modified_settings);

        string error_msg;
        bool changed = execute_cmd_time_limit(change_master, time_remaining, &error_msg);
        time_remaining -= timer.restart();
        if (changed)
        {
            string start = string_printf("START SLAVE '%s';", conn_name.c_str());
            bool started = execute_cmd_time_limit(start, time_remaining, &error_msg);
            time_remaining -= timer.restart();
            if (started)
            {
                success = true;
            }
            else
            {
                PRINT_MXS_JSON_ERROR(error_out,
                                     "%s could not be started: %s",
                                     modified_settings.to_string().c_str(), error_msg.c_str());
            }
        }
        else
        {
            // TODO: This may currently print out passwords.
            PRINT_MXS_JSON_ERROR(error_out,
                                 "%s could not be redirected to %s: %s",
                                 conn_settings.to_string().c_str(),
                                 modified_settings.master_endpoint.to_string().c_str(),
                                 error_msg.c_str());
        }
    }   // 'stop_slave_conn' prints its own errors
    return success;
}

bool MariaDBServer::update_enabled_events()
{
    string error_msg;
    // Get names of all enabled scheduled events on the server.
    auto event_info = execute_query("SELECT Event_schema, Event_name FROM information_schema.EVENTS WHERE "
                                    "Status = 'ENABLED';", &error_msg);
    if (event_info.get() == NULL)
    {
        MXS_ERROR("Could not query events of '%s': %s Event handling can be disabled by "
                  "setting '%s' to false.",
                  name(), error_msg.c_str(), CN_HANDLE_EVENTS);
        return false;
    }

    auto db_name_ind = 0;
    auto event_name_ind = 1;

    EventNameSet full_names;
    full_names.reserve(event_info->get_row_count());

    while (event_info->next_row())
    {
        string full_name = event_info->get_string(db_name_ind) + "." + event_info->get_string(event_name_ind);
        full_names.insert(full_name);   // Ignore duplicates, they shouldn't exists.
    }

    m_enabled_events = std::move(full_names);

    return true;
}

/**
 * Connect to and query/update a server.
 *
 * @param server The server to update
 */
void MariaDBServer::update_server(bool time_to_update_disk_space)
{
    auto server = this;
    m_new_events.clear();
    MonitorServer* mon_srv = server;
    ConnectResult conn_status = mon_srv->ping_or_connect();

    if (mxs::Monitor::connection_is_ok(conn_status))
    {
        mon_srv->maybe_fetch_server_variables();
        set_status(SERVER_RUNNING);
        const bool new_connection = (conn_status == ConnectResult::NEWCONN_OK);
        if (new_connection)
        {
            // Is a new connection or a reconnection. Check server version.
            update_server_version();
            clear_locks_info();     // Lock expired due to lost connection.
        }

        if (m_capabilities.basic_support)
        {
            // Check permissions if permissions failed last time or if this is a new connection.
            if (had_status(SERVER_AUTH_ERROR) || new_connection)
            {
                check_permissions();
            }

            // If permissions are ok, continue.
            if (!server->has_status(SERVER_AUTH_ERROR))
            {
                if (time_to_update_disk_space && mon_srv->can_update_disk_space_status())
                {
                    mon_srv->update_disk_space_status();
                }

                if (m_settings.server_locks_enabled)
                {
                    // Update lock status every tick. This is especially required for the secondary MaxScale,
                    // as it needs to quickly react if the primary dies.
                    update_locks_status();
                }

                // Query MariaDBServer specific data
                server->monitor_server();
            }
        }
    }
    else
    {
        /* The current server is not running. Clear some of the bits. User-set bits and some long-term bits
         * can stay. */
        server->clear_status(MonitorServer::SERVER_DOWN_CLEAR_BITS);
        clear_locks_info();

        if (conn_status == ConnectResult::ACCESS_DENIED)
        {
            server->set_status(SERVER_AUTH_ERROR);
        }

        /* Log connect failure only once, that is, if server was RUNNING or MAINTENANCE during last
         * iteration. */
        if (server->had_status(SERVER_RUNNING) || server->had_status(SERVER_MAINT))
        {
            mon_srv->log_connect_error(conn_status);
        }
    }

    /** Increase or reset the error count of the server. */
    bool is_running = server->is_running();
    bool in_maintenance = server->is_in_maintenance();
    mon_srv->mon_err_count = (is_running || in_maintenance) ? 0 : mon_srv->mon_err_count + 1;
}


bool MariaDBServer::kick_out_super_users(GeneralOpData& op)
{
    bool error = false;
    Duration time_remaining = op.time_remaining;
    auto error_out = op.error_out;
    // Only select unique rows...
    string get_ids_query = "SELECT DISTINCT * FROM ("
        // select conn id and username from live connections ...
                           "SELECT P.id,P.user FROM information_schema.PROCESSLIST as P "
        // match with user information ...
                           "INNER JOIN mysql.user as U ON (U.user = P.user) WHERE "
        // where the user has super-privileges, is not replicating ...
                           "(U.Super_priv = 'Y' AND P.COMMAND != 'Binlog Dump' "
        // and is not the current user.
                           "AND P.id != (SELECT CONNECTION_ID()))) as I;";

    string error_msg;
    unsigned int error_num = 0;
    auto res = execute_query(get_ids_query, &error_msg, &error_num);
    if (res)
    {
        int id_col = 0;
        int user_col = 1;
        while (res->next_row())
        {
            auto conn_id = res->get_int(id_col);
            auto user = res->get_string(user_col);
            string kill_query = mxb::string_printf("KILL SOFT CONNECTION %li;", conn_id);
            StopWatch timer;
            if (execute_cmd_time_limit(kill_query, time_remaining, &error_msg))
            {
                MXB_WARNING("Killed connection id %lu to '%s' from super-user '%s' to prevent writes.",
                            conn_id, name(), user.c_str());
            }
            else
            {
                error = true;
                PRINT_MXS_JSON_ERROR(error_out, "Could not kill connection %lu from super-user '%s': %s",
                                     conn_id, user.c_str(), error_msg.c_str());
            }
            time_remaining -= timer.split();
        }
    }
    else
    {
        // If query failed because of insufficient rights, don't consider this an error, just print a warning.
        // Perhaps the user doesn't want the monitor doing this.
        if (error_num == ER_DBACCESS_DENIED_ERROR || error_num == ER_TABLEACCESS_DENIED_ERROR
            || error_num == ER_COLUMNACCESS_DENIED_ERROR)
        {
            MXB_WARNING("Insufficient rights to query logged in super-users for server '%s': %s Super-users "
                        "may perform writes during the cluster manipulation operation.",
                        name(), error_msg.c_str());
        }
        else
        {
            error = true;
            PRINT_MXS_JSON_ERROR(error_out, "Could not query connected super-users: %s", error_msg.c_str());
        }
    }
    return !error;
}

void MariaDBServer::update_locks_status()
{
    /* Read a lock status from a result row. */
    auto read_lock_status = [this](const QueryResult& is_used_row, int ind) {
            ServerLock rval;
            if (is_used_row.field_is_null(ind))
            {
                // null means the lock is free.
                rval.set_status(ServerLock::Status::FREE);
            }
            else
            {
                auto lock_owner_id = is_used_row.get_int(ind);
                // Either owned by this MaxScale or another.
                auto new_status = (lock_owner_id == conn_id()) ? ServerLock::Status::OWNED_SELF :
                    ServerLock::Status::OWNED_OTHER;
                rval.set_status(new_status, lock_owner_id);
            }
            return rval;
        };

    auto report_unexpected_lock = [this](ServerLock old_status, ServerLock new_status,
                                         const string& lock_name) {
            bool owned_lock = (old_status.status() == ServerLock::Status::OWNED_SELF);
            if (new_status.status() == ServerLock::Status::OWNED_SELF)
            {
                // This MaxScale has the lock. Print warning if it got the lock without knowing it.
                if (!owned_lock)
                {
                    MXB_WARNING("Acquired the lock '%s' on server '%s' without locking it.",
                                lock_name.c_str(), name());
                }
            }
            else
            {
                // Don't have the lock. Print a warning if lock was lost without releasing it.
                // This may happen if connection broke and was recreated.
                if (owned_lock)
                {
                    string msg = string_printf("Lost the lock '%s' on server '%s' without releasing it.",
                                               lock_name.c_str(), name());
                    if (new_status.status() == ServerLock::Status::OWNED_OTHER)
                    {
                        msg += string_printf(" The lock is now owned by connection %li.", new_status.owner());
                    }
                    MXB_WARNING("%s", msg.c_str());
                }
            }
        };

    // First, check who currently has the locks. If the query fails, assume that this MaxScale does not
    // have the locks. This is correct if connection failed.
    string cmd = string_printf("SELECT IS_USED_LOCK('%s'), IS_USED_LOCK('%s');",
                               SERVER_LOCK_NAME, MASTER_LOCK_NAME);
    string err_msg;
    ServerLock serverlock_status_new;
    ServerLock masterlock_status_new;
    auto res_is_used = execute_query(cmd, &err_msg);

    if (res_is_used && res_is_used->get_col_count() == 2 && res_is_used->next_row())
    {
        serverlock_status_new = read_lock_status(*res_is_used, 0);
        report_unexpected_lock(m_serverlock, serverlock_status_new, SERVER_LOCK_NAME);

        masterlock_status_new = read_lock_status(*res_is_used, 1);
        report_unexpected_lock(m_masterlock, masterlock_status_new, MASTER_LOCK_NAME);
        // Masterlock is not acquired here, only status is updated.
    }

    m_serverlock = serverlock_status_new;
    m_masterlock = masterlock_status_new;
    if (!err_msg.empty())
    {
        MXB_ERROR("Failed to update lock status of server '%s'. %s", name(), err_msg.c_str());
    }
}

/**
 * Release a server lock.
 *
 * @param lock_type Which lock to release
 * @return True if lock was released normally. False does not mean lock is held, as it may not have been
 * held to begin with.
 */
bool MariaDBServer::release_lock(LockType lock_type)
{
    bool normal_lock = (lock_type == LockType::SERVER);
    ServerLock* output = normal_lock ? &m_serverlock : &m_masterlock;
    const char* lockname = normal_lock ? SERVER_LOCK_NAME : MASTER_LOCK_NAME;

    // Try to release the lock.
    string cmd = string_printf("SELECT RELEASE_LOCK('%s')", lockname);
    string err_msg;
    ServerLock lock_result;
    bool rval = false;

    auto res_release_lock = execute_query(cmd, &err_msg);
    if (res_release_lock && res_release_lock->get_col_count() == 1 && res_release_lock->next_row())
    {
        if (res_release_lock->field_is_null(0))
        {
            // Lock did not exist and can be considered free.
            lock_result.set_status(ServerLock::Status::FREE);
        }
        else
        {
            auto res_num = res_release_lock->get_int(0);
            if (res_num == 1)
            {
                // Expected. Lock was owned by this connection and is released.
                lock_result.set_status(ServerLock::Status::FREE);
                rval = true;
            }
            else
            {
                // Lock was owned by another connection and was not freed. The owner is unknown.
                lock_result.set_status(ServerLock::Status::OWNED_OTHER);
            }
        }
    }
    else
    {
        MXS_ERROR("Failed to release lock on server '%s'. %s", name(), err_msg.c_str());
    }

    *output = lock_result;
    return rval;
}

bool MariaDBServer::get_lock(LockType lock_type)
{
    bool normal_lock = (lock_type == LockType::SERVER);
    ServerLock* output = normal_lock ? &m_serverlock : &m_masterlock;
    const char* lockname = normal_lock ? SERVER_LOCK_NAME : MASTER_LOCK_NAME;

    bool rval = false;
    string cmd = string_printf("SELECT GET_LOCK('%s', 0)", lockname);
    string err_msg;
    ServerLock lock_result;
    auto res_get_lock = execute_query(cmd, &err_msg);
    const int column = 0;

    if (res_get_lock && res_get_lock->get_col_count() == 1 && res_get_lock->next_row())
    {
        // If the result is NULL, an error occurred.
        if (!res_get_lock->field_is_null(column))
        {
            auto lock_res = res_get_lock->get_int(column);
            if (lock_res == 1)
            {
                // Got the lock.
                lock_result.set_status(ServerLock::Status::OWNED_SELF, con->thread_id);
                rval = true;
            }
            else
            {
                // Someone else got to it first. Owner unknown.
                lock_result.set_status(ServerLock::Status::OWNED_OTHER);
            }
        }
    }
    else
    {
        MXB_ERROR("Failed to acquire lock on server '%s'. %s", name(), err_msg.c_str());
    }

    *output = lock_result;
    return rval;
}

bool MariaDBServer::lock_owned(LockType lock_type)
{
    if (lock_type == LockType::SERVER)
    {
        return m_serverlock.status() == ServerLock::Status::OWNED_SELF;
    }
    else
    {
        return m_masterlock.status() == ServerLock::Status::OWNED_SELF;
    }
}

/**
 * Release both types of locks held on the server.
 *
 * @return How many locks were held and then released
 */
int MariaDBServer::release_all_locks()
{
    int normal_releases = 0;
    for (auto lock_type : {MariaDBServer::LockType::SERVER, MariaDBServer::LockType::MASTER})
    {
        if (lock_owned(lock_type))
        {
            normal_releases += release_lock(lock_type);
        }
    }
    return normal_releases;
}

int64_t MariaDBServer::conn_id() const
{
    return con ? con->thread_id : -1;
}

bool MariaDBServer::marked_as_master(string* why_not) const
{
    bool rval = true;
    if (m_masterlock.status() != ServerLock::Status::OWNED_OTHER)
    {
        rval = false;
        if (why_not)
        {
            *why_not = "it's not marked as master by the primary MaxScale";
        }
    }
    else if (!(m_masterlock == m_serverlock))
    {
        rval = false;
        if (why_not)
        {
            *why_not = "the normal lock and master lock are claimed by different connection id:s";
        }
    }
    return rval;
}

void MariaDBServer::clear_locks_info()
{
    m_serverlock.set_status(ServerLock::Status::UNKNOWN);
    m_masterlock.set_status(ServerLock::Status::UNKNOWN);
}

ServerLock MariaDBServer::masterlock_status() const
{
    return m_masterlock;
}

ServerLock MariaDBServer::serverlock_status() const
{
    return m_serverlock;
}

ServerLock MariaDBServer::lock_status(LockType locktype) const
{
    return (locktype == LockType::SERVER) ? m_serverlock : m_masterlock;
}

SERVER::VersionInfo::Type MariaDBServer::server_type() const
{
    return server->info().type();
}

void MariaDBServer::update_rlag_state(int64_t limit)
{
    mxb_assert(limit >= 0);
    using mxs::RLagState;
    auto rlag_now = m_replication_lag;
    // Only change the state if rlag could be read.
    if (rlag_now != mxs::Target::RLAG_UNDEFINED)
    {
        auto new_state = (rlag_now > limit) ? RLagState::ABOVE_LIMIT : RLagState::BELOW_LIMIT;
        if (new_state != m_rlag_state)
        {
            m_rlag_state = new_state;
            string new_event = (new_state == RLagState::ABOVE_LIMIT) ? "rlag_above" : "rlag_below";
            m_new_events.push_back(move(new_event));
        }
    }
}

const MonitorServer::EventList& MariaDBServer::new_custom_events() const
{
    return m_new_events;
}
