/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/replication_cluster.hh>

#include <iostream>
#include <thread>
#include <maxbase/format.hh>
#include <maxbase/stopwatch.hh>
#include <maxtest/log.hh>
#include <maxtest/mariadb_connector.hh>

using std::string;
using std::cout;
using std::endl;
using ServerArray = std::vector<mxt::MariaDBServer*>;

namespace
{
const string type_mariadb = "mariadb";
const string my_nwconf_prefix = "node";
const string my_name = "Master-Slave-cluster";

const char create_repl_user[] =
    "grant replication slave on *.* to repl@'%%' identified by 'repl'; "
    "FLUSH PRIVILEGES";
const char setup_slave[] =
    "change master to MASTER_HOST='%s', MASTER_PORT=%d, "
    "MASTER_USER='repl', MASTER_PASSWORD='repl', "
    "MASTER_USE_GTID=current_pos; "
    "start slave;";

const string sl_io = "Slave_IO_Running";
const string sl_sql = "Slave_SQL_Running";
const string show_slaves = "show all slaves status;";

const string repl_user = "repl";
const string repl_pw = "repl";

bool repl_thread_run_states_ok(const string& io, const string& sql)
{
    return (io == "Yes" || io == "Connecting" || io == "Preparing") && sql == "Yes";
}

bool is_writable(mxt::MariaDB* conn)
{
    bool rval = false;
    auto res = conn->try_query("select @@read_only;");
    if (res && res->next_row() && res->get_bool(0) == false)
    {
        rval = true;
    }
    return rval;
}
}

namespace maxtest
{
ReplicationCluster::ReplicationCluster(SharedData* shared)
    : MariaDBCluster(shared, "server")
{
}

const std::string& ReplicationCluster::type_string() const
{
    return type_mariadb;
}

bool ReplicationCluster::start_replication()
{
    const int n = N;
    // Generate users on all nodes.
    // TODO: most users can be generated just on the master once replication is on.
    for (int i = 0; i < n; i++)
    {
        create_users(i);
    }

    ping_or_open_admin_connections();

    // At this point, the servers have conflicting gtids but identical data. Set gtids manually so
    // replication can start.
    bool reset_ok = true;
    for (int i = 0; i < n; i++)
    {
        auto conn = backend(i)->admin_connection();
        if (!conn->try_cmd("RESET MASTER;") || !conn->try_cmd("SET GLOBAL gtid_slave_pos='0-1-0'"))
        {
            reset_ok = false;
        }
    }

    bool rval = false;
    if (reset_ok)
    {
        bool repl_ok = true;
        // Finally, begin replication.
        string change_master = gen_change_master_cmd(backend(0));
        for (int i = 1; i < n; i++)
        {
            auto conn = backend(i)->admin_connection();
            if (!conn->try_cmd(change_master) || !conn->try_cmd("START SLAVE;"))
            {
                repl_ok = false;
            }
        }

        if (repl_ok)
        {
            rval = true;
        }
    }

    return rval;
}

bool ReplicationCluster::check_replication()
{
    const bool verbose = this->verbose();
    if (verbose)
    {
        logger().log_msgf("Checking %s", my_name.c_str());
    }

    auto check_disable_read_only = [this](mxt::MariaDBServer* srv) {
            bool rval = false;
            auto conn = srv->admin_connection();
            if (is_writable(conn))
            {
                rval = true;
            }
            else
            {
                logger().log_msgf("%s is in read-only mode, trying to disable.",
                                  srv->vm_node().m_name.c_str());
                if (conn->try_cmd("set global read_only=0;") && is_writable(conn))
                {
                    rval = true;
                    logger().log_msgf("Read-only disabled on %s", srv->vm_node().m_name.c_str());
                }
            }
            return rval;
        };

    const int n = N;
    bool all_writable = true;
    for (int i = 0; i < n; i++)
    {
        if (!check_disable_read_only(backend(i)))
        {
            all_writable = false;
        }
    }

    bool res = false;
    if (all_writable)
    {
        // Check that the supposed master is not replicating. If it is, remove the slave connection.
        auto master = backend(0);
        if (remove_all_slave_conns(master))
        {
            bool repl_set_up = true;
            // Master ok, check slaves.
            for (int i = 1; i < n; i++)
            {
                if (!good_slave_thread_status(backend(i), master))
                {
                    repl_set_up = false;
                }
            }

            if (repl_set_up)
            {
                // Replication should be ok, but test it by writing an event to master.
                if (master->admin_connection()->try_cmd("flush tables;") && sync_slaves(0))
                {
                    res = true;
                }
            }
        }
    }

    logger().log_msgf("%s %s.", my_name.c_str(), res ? "replicating" : "not replicating.");
    return res;
}

bool ReplicationCluster::remove_all_slave_conns(MariaDBServer* server)
{
    bool rval = false;
    auto conn = server->admin_connection();
    auto name = server->vm_node().m_name.c_str();
    auto res = conn->try_query(show_slaves);
    if (res)
    {
        int rows = res->get_row_count();
        if (rows == 0)
        {
            rval = true;
        }
        else
        {
            logger().log_msgf("%s has %i slave connection(s), removing them.", name, rows);
            if (conn->try_cmd("stop all slaves;"))
            {
                while (res->next_row())
                {
                    string conn_name = res->get_string("Connection_name");
                    string reset = mxb::string_printf("reset slave '%s' all;", conn_name.c_str());
                    conn->try_cmd(reset);
                }

                auto res2 = conn->try_query(show_slaves);
                if (res2->get_row_count() == 0)
                {
                    rval = true;
                    logger().log_msgf("Slave connection(s) removed from %s.", name);
                }
            }
        }
    }
    return rval;
}

/**
 * Check replication connection status
 *
 * @return True if all is well
 */
bool ReplicationCluster::good_slave_thread_status(MariaDBServer* slave, MariaDBServer* master)
{
    auto is_replicating_from_master = [this, slave, master](mxq::QueryResult* res) {
            auto namec = slave->vm_node().m_name.c_str();
            string conn_name = res->get_string("Connection_name");
            string host = res->get_string("Master_Host");
            int port = res->get_int("Master_Port");

            bool rval = false;
            if (conn_name.empty() && host == master->vm_node().priv_ip() && port == master->port())
            {
                string io_running = res->get_string(sl_io);
                string sql_running = res->get_string(sl_sql);

                if (repl_thread_run_states_ok(io_running, sql_running))
                {
                    string using_gtid = res->get_string("Using_Gtid");
                    if (using_gtid == "Slave_Pos" || using_gtid == "Current_Pos")
                    {
                        rval = true;
                    }
                    else
                    {
                        logger().log_msgf("%s is not using gtid in replication.", namec);
                    }
                }
                else
                {
                    logger().log_msgf("Replication threads of %s are not in expected states. "
                                      "IO: '%s', SQL: '%s'", namec, io_running.c_str(), sql_running.c_str());
                }
            }
            else
            {
                logger().log_msgf("%s is not replicating from master or the replication is not in standard "
                                  "configuration.", namec);
            }
            return rval;
        };

    bool recreate = false;
    bool error = false;

    auto conn = slave->admin_connection();
    auto res = conn->try_query(show_slaves);
    if (res)
    {
        int rows = res->get_row_count();
        if (rows > 1)
        {
            // Multisource replication, remove connections.
            if (remove_all_slave_conns(slave))
            {
                recreate = true;
            }
            else
            {
                error = true;
            }
        }
        else if (rows == 1)
        {
            res->next_row();
            if (!is_replicating_from_master(res.get()))
            {
                if (remove_all_slave_conns(slave))
                {
                    recreate = true;
                }
                else
                {
                    error = true;
                }
            }
        }
        else
        {
            // No connection, create one.
            recreate = true;
        }
    }
    else
    {
        error = true;
    }

    bool rval = false;
    if (!error)
    {
        if (recreate)
        {
            string change_cmd = gen_change_master_cmd(master);
            if (conn->try_cmd(change_cmd) && conn->try_cmd("start slave;"))
            {
                // Replication should be starting. Give the slave some time to get started, then check that
                // replication is at least starting.
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                res = conn->try_query(show_slaves);
                if (res && res->get_row_count() == 1)
                {
                    res->next_row();
                    if (is_replicating_from_master(res.get()))
                    {
                        rval = true;
                    }
                }
            }
        }
        else
        {
            rval = true;
        }
    }

    return rval;
}

bool ReplicationCluster::sync_slaves(int master_node_ind)
{
    struct Gtid
    {
        int64_t domain {-1};
        int64_t server_id {-1};
        int64_t seq_no {-1};

        bool operator==(const Gtid& rhs) const
        {
            return domain == rhs.domain && server_id == rhs.server_id && seq_no == rhs.seq_no;
        }
    };

    struct ReplData
    {
        Gtid gtid;
        bool repl_configured {false};
        bool is_replicating {false};
    };

    auto update_one_server = [](mxt::MariaDBServer* server) {
            ReplData rval;
            auto conn = server->admin_connection();
            if (conn->is_open())
            {
                auto res = conn->multiquery({"select @@gtid_current_pos;", "show all slaves status;"});
                if (!res.empty())
                {
                    // Got results. When parsing gtid, only consider the first triplet. Typically that's all
                    // there is.
                    auto& res_gtid = res[0];
                    if (res_gtid->next_row())
                    {
                        string gtid_current = res_gtid->get_string(0);
                        gtid_current = cutoff_string(gtid_current, ',');
                        auto elems = mxb::strtok(gtid_current, "-");
                        if (elems.size() == 3)
                        {
                            mxb::get_long(elems[0], &rval.gtid.domain);
                            mxb::get_long(elems[1], &rval.gtid.server_id);
                            mxb::get_long(elems[2], &rval.gtid.seq_no);
                        }
                    }

                    auto& slave_ss = res[1];
                    if (slave_ss->next_row())
                    {
                        rval.repl_configured = true;
                        string io_state = slave_ss->get_string(sl_io);
                        string sql_state = slave_ss->get_string(sl_sql);
                        rval.is_replicating = repl_thread_run_states_ok(io_state, sql_state);
                    }
                }
            }
            return rval;
        };

    auto update_all = [this, &update_one_server](const ServerArray& servers) {
            size_t n = servers.size();
            std::vector<ReplData> rval;
            rval.resize(n);

            mxt::BoolFuncArray funcs;
            funcs.reserve(n);

            for (size_t i = 0; i < n; i++)
            {
                auto func = [&rval, &servers, i, &update_one_server]() {
                        rval[i] = update_one_server(servers[i]);
                        return true;
                    };
                funcs.push_back(std::move(func));
            }
            m_shared.concurrent_run(funcs);
            return rval;
        };

    ping_or_open_admin_connections();
    auto master = backend(master_node_ind);
    Gtid master_gtid = update_one_server(master).gtid;

    bool rval = false;

    if (master_gtid.server_id < 0)
    {
        m_shared.log.log_msgf("Could not read gtid from master %s when waiting for cluster sync.",
                              master->vm_node().m_name.c_str());
    }
    else
    {
        std::vector<MariaDBServer*> waiting_catchup;
        waiting_catchup.reserve(N - 1);
        for (int i = 0; i < N; i++)
        {
            auto srv = backend(i);
            if (srv != master && srv->admin_connection()->is_open())
            {
                waiting_catchup.push_back(srv);
            }
        }
        int expected_catchups = waiting_catchup.size();
        int successful_catchups = 0;
        mxb::StopWatch timer;
        auto limit = mxb::from_secs(10);    // Wait a maximum of 10 seconds for sync.

        while (!waiting_catchup.empty() && timer.split() < limit)
        {
            auto repl_data = update_all(waiting_catchup);
            if (verbose())
            {
                logger().log_msgf("Waiting for %zu servers to sync with master.",
                                  waiting_catchup.size());
            }

            for (size_t i = 0; i < waiting_catchup.size();)
            {
                auto& elem = repl_data[i];
                bool sync_possible = false;
                bool in_sync = false;

                if (elem.gtid.server_id < 0)
                {
                    // Query or connection failed. Cannot sync.
                }
                else if (elem.gtid == master_gtid)
                {
                    in_sync = true;
                }
                else if (!elem.repl_configured)
                {
                    // Not in matching gtid and no replication configured. Cannot sync.
                }
                else if (elem.gtid.domain != master_gtid.domain)
                {
                    // If a test uses complicated gtid:s, it needs to handle it on it's own.
                    m_shared.log.log_msgf("Found different gtid domain id:s (%li and %li) when waiting "
                                          "for cluster sync.", elem.gtid.domain, master_gtid.domain);
                }
                else if (elem.is_replicating)
                {
                    sync_possible = true;
                }

                if (in_sync || !sync_possible)
                {
                    waiting_catchup.erase(waiting_catchup.begin() + i);
                    repl_data.erase(repl_data.begin() + i);
                    if (in_sync)
                    {
                        successful_catchups++;
                    }
                }
                else
                {
                    i++;
                }
            }

            if (!waiting_catchup.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }

        if (successful_catchups == expected_catchups)
        {
            rval = true;
            if (verbose())
            {
                logger().log_msgf("Slave sync took %.1f seconds.", mxb::to_secs(timer.split()));
            }
        }
        else
        {
            logger().log_msgf("Only %i out of %i servers in the cluster got in sync within %.1f seconds.",
                              successful_catchups, expected_catchups, mxb::to_secs(timer.split()));
        }
    }
    return rval;
}

void ReplicationCluster::change_master(int NewMaster, int OldMaster)
{
    for (int i = 0; i < N; i++)
    {
        if (mysql_ping(nodes[i]) == 0)
        {
            execute_query(nodes[i], "STOP SLAVE");
        }
    }

    execute_query(nodes[NewMaster], "RESET SLAVE ALL");
    execute_query(nodes[NewMaster], "%s", create_repl_user);

    if (mysql_ping(nodes[OldMaster]) == 0)
    {
        execute_query(nodes[OldMaster], "RESET MASTER");
    }

    for (int i = 0; i < N; i++)
    {
        if (i != NewMaster && mysql_ping(nodes[i]) == 0)
        {
            char str[1024];
            sprintf(str, setup_slave, ip_private(NewMaster), port[NewMaster]);
            execute_query(nodes[i], "%s", str);
        }
    }
}

void ReplicationCluster::replicate_from(int slave, int master)
{
    replicate_from(slave, ip_private(master), port[master]);
}

void ReplicationCluster::replicate_from(int slave, const std::string& host, uint16_t port)
{
    replicate_from(slave, host, port, GtidType::CURRENT_POS, "", false);
}

void ReplicationCluster::replicate_from(int slave, const std::string& host, uint16_t port, GtidType type,
                                        const std::string& conn_name, bool reset)
{
    auto be = backend(slave);
    if (be->ping_or_open_admin_connection())
    {
        auto conn_namec = conn_name.c_str();
        auto conn = be->admin_connection();
        if (conn->cmd_f("STOP SLAVE '%s';", conn_namec))
        {
            if (reset)
            {
                conn->cmd_f("RESET SLAVE '%s' ALL;", conn_namec);
            }
            const char* gtid_str = (type == GtidType::CURRENT_POS) ? "current_pos" : "slave_pos";
            string change_master = mxb::string_printf(
                "CHANGE MASTER '%s' TO MASTER_HOST = '%s', MASTER_PORT = %i, "
                "MASTER_USER = '%s', MASTER_PASSWORD = '%s', MASTER_USE_GTID = %s;",
                conn_namec, host.c_str(), port, repl_user.c_str(), repl_pw.c_str(), gtid_str);
            conn->cmd(change_master);
            conn->cmd_f("START SLAVE '%s';", conn_namec);
        }
    }
}

const std::string& ReplicationCluster::nwconf_prefix() const
{
    return my_nwconf_prefix;
}

const std::string& ReplicationCluster::name() const
{
    return my_name;
}

std::string ReplicationCluster::get_srv_cnf_filename(int node)
{
    return mxb::string_printf("server%i.cnf", node + 1);
}

std::string ReplicationCluster::gen_change_master_cmd(MariaDBServer* master)
{
    return mxb::string_printf("change master to master_host='%s', master_port=%i, master_user='%s', "
                              "master_password='%s', master_use_gtid=slave_pos;",
                              master->vm_node().priv_ip(), master->port(), "repl", "repl");
}

bool ReplicationCluster::create_users(int i)
{
    bool rval = false;
    if (create_base_users(i))
    {
        auto be = backend(i);
        auto vrs = be->version();

        mxt::MariaDBUserDef mdbmon_user = {"mariadbmon", "%", "mariadbmon"};
        mdbmon_user.grants = {"SUPER, FILE, RELOAD, PROCESS, SHOW DATABASES, EVENT ON *.*",
                              "SELECT ON mysql.user"};
        if (vrs.major == 10 && vrs.minor >= 5)
        {
            mdbmon_user.grants.emplace_back("REPLICATION SLAVE ADMIN ON *.*");
        }
        else
        {
            mdbmon_user.grants.emplace_back("REPLICATION CLIENT ON *.*");
        }

        bool error = false;
        auto ssl = ssl_mode();
        if (!be->create_user(mdbmon_user, ssl)
            || !be->create_user(service_user_def(), ssl)
            || !be->admin_connection()->try_cmd("GRANT REPLICATION SLAVE ON *.* TO 'repl'@'%';"))
        {
            error = true;
        }

        if (vrs.major == 10 && ((vrs.minor == 5 && vrs.patch >= 8) || ( vrs.minor >= 6 )))
        {
            if (!be->admin_connection()->try_cmd("GRANT SLAVE MONITOR ON *.* TO 'repl'@'%';"))
            {
                error = true;
            }
        }

        if (!error)
        {
            rval = true;
        }
    }

    return rval;
}
}
