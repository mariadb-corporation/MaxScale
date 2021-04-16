/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
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

bool is_readonly(mxt::MariaDB* conn)
{
    bool rval = false;
    auto res = conn->try_query("select @@read_only;");
    if (res && res->next_row() && res->get_bool(0) == true)
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

int ReplicationCluster::start_replication()
{
    int local_result = 0;

    // Start all nodes
    for (int i = 0; i < N; i++)
    {
        if (start_node(i, (char*) ""))
        {
            printf("Start of node %d failed\n", i);
            return 1;
        }

        create_users(i);
    }

    robust_connect(10);

    string change_master = mxb::string_printf(
        "change master to master_host='%s', master_port=%i, master_user='repl', master_password='repl', "
        "master_use_gtid=slave_pos;",
        ip_private(0), port[0]);

    for (int i = 0; i < N; i++)
    {
        execute_query(nodes[i], "SET GLOBAL read_only=OFF");
        execute_query(nodes[i], "STOP SLAVE;");
        execute_query(nodes[i], "SET GLOBAL gtid_slave_pos='0-1-0'");

        if (i != 0)
        {
            execute_query(nodes[i], "%s", change_master.c_str());
            execute_query(nodes[i], "START SLAVE");
        }
    }

    disconnect();

    return local_result;
}

bool ReplicationCluster::check_replication()
{
    const bool verbose = this->verbose();
    if (verbose)
    {
        logger().log_msgf("Checking %s", my_name.c_str());
    }

    if (!update_status())
    {
        cout << "Failed to update status" << endl;
        return false;
    }

    bool res = true;
    int master_ind = 0;     // The first server should be master.
    for (int i = 0; i < N && res; i++)
    {
        if (ssl && !check_ssl(i))
        {
            res = false;
        }

        auto srv = backend(i);
        auto conn = srv->try_open_admin_connection();
        if (conn->is_open())
        {
            auto res1 = conn->try_query("SELECT COUNT(*) FROM mysql.user;");
            if (!res1)
            {
                res = false;
            }

            auto conn_ptr = conn.get();
            if (i == master_ind)
            {
                if (check_master_node(conn_ptr))
                {
                    // To ensure replication is working, add an event to the master, then check for sync.
                    if (!conn->try_cmd("flush tables;"))
                    {
                        res = false;
                    }
                }
                else
                {
                    res = false;
                    logger().log_msgf("Master node check failed for node %d.", i);
                }
            }
            else
            {
                if (!good_slave_thread_status(conn_ptr, i) || is_readonly(conn_ptr))
                {
                    res = false;
                    if (verbose)
                    {
                        printf("Slave %d check failed\n", i);
                    }
                }
            }
        }
        else
        {
            res = false;
        }
    }

    if (res && !sync_slaves(master_ind))
    {
        res = false;
    }

    if (verbose)
    {
        logger().log_msgf("Master-Slave cluster %s.", res ? "replicating" : "not replicating.");
    }
    return res;
}

bool ReplicationCluster::check_master_node(mxt::MariaDB* conn)
{
    bool rval = false;
    auto res = conn->try_query("show all slaves status;");
    if (res)
    {
        if (res->get_row_count() == 0)
        {
            if (!is_readonly(conn))
            {
                rval = true;
            }
            else
            {
                logger().log_msg("The master is in read-only mode.");
            }
        }
        else
        {
            logger().log_msg("The master is configured as a slave.");
        }
    }
    return rval;
}

/**
 * Check replication connection status
 *
 * @param conn MYSQL struct (connection have to be open)
 * @param node Node index
 * @return True if all is well
 */
bool ReplicationCluster::good_slave_thread_status(mxt::MariaDB* conn, int node)
{
    bool rval = false;
    bool multisource_repl = false;
    bool gtid_ok = false;
    string io_running;
    string sql_running;

    const string y = "Yes";
    const string n = "No";

    // Doing 3 attempts to check status
    for (int i = 0; i < 2; i++)
    {
        auto res = conn->try_query("show all slaves status;");
        if (res)
        {
            int rows = res->get_row_count();
            if (rows != 1)
            {
                if (rows > 1)
                {
                    multisource_repl = true;
                }
                break;
            }
            else
            {
                res->next_row();
                io_running = res->get_string(sl_io);
                sql_running = res->get_string(sl_sql);
                if (!io_running.empty() && io_running != y && io_running != n)
                {
                    // May not be final value, wait and try again.
                    sleep(1);
                }
                else
                {
                    string gtid_io = res->get_string("Gtid_IO_Pos");
                    string using_gtid = res->get_string("Using_Gtid");
                    if (!gtid_io.empty() && !using_gtid.empty())
                    {
                        gtid_ok = true;
                    }
                    break;
                }
            }
        }
    }

    if (!gtid_ok)
    {
        logger().log_msgf("Node %d: Not using gtid replication.", node);
    }
    else if (multisource_repl)
    {
        logger().log_msgf("Node %d: More than one configured slave.", node);
    }
    else
    {
        if (io_running == y && sql_running == y)
        {
            rval = true;
        }
        else
        {
            logger().log_msgf("Node %d: Slave_IO_Running: '%s', Slave_SQL_Running: '%s'",
                              node, io_running.c_str(), sql_running.c_str());
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
    };

    struct ReplData
    {
        Gtid gtid;
        bool is_replicating {false};
    };

    auto update_one_server = [](mxt::MariaDB* conn) {
            ReplData rval;
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
                        string io_state = slave_ss->get_string(sl_io);
                        string sql_state = slave_ss->get_string(sl_sql);
                        rval.is_replicating = (io_state != "No" && sql_state == "Yes");
                    }
                }
            }
            return rval;
        };

    auto update_all = [this, &update_one_server](const ConnArray& conns) {
            int n = conns.size();
            std::vector<ReplData> rval;
            rval.resize(n);

            mxt::BoolFuncArray funcs;
            funcs.reserve(n);

            for (int i = 0; i < n; i++)
            {
                auto func = [&rval, &conns, i, &update_one_server]() {
                        rval[i] = update_one_server(conns[i].get());
                        return true;
                    };
                funcs.push_back(std::move(func));
            }
            m_shared.concurrent_run(funcs);
            return rval;
        };

    auto master = backend(master_node_ind);
    auto master_conn = master->try_open_admin_connection();
    Gtid best_found = update_one_server(master_conn.get()).gtid;

    bool rval = false;

    if (best_found.server_id < 0)
    {
        m_shared.log.log_msgf("Could not read valid gtid:s when waiting for cluster sync.");
    }
    else
    {
        auto waiting_catchup = admin_connect_to_all();
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
                                  waiting_catchup.size() - 1);
            }

            for (size_t i = 0; i < waiting_catchup.size();)
            {
                auto& elem = repl_data[i];
                bool sync_possible = false;
                bool in_sync = false;

                if (elem.gtid.server_id < 0)
                {
                    // Query or connection failed.
                }
                else if (elem.gtid.domain != best_found.domain)
                {
                    // If a test uses complicated gtid:s, it needs to handle it on it's own.
                    m_shared.log.log_msgf("Found different gtid domain id:s (%li and %li) when waiting "
                                          "for cluster sync.", elem.gtid.domain, best_found.domain);
                }
                else if (elem.gtid.seq_no >= best_found.seq_no)
                {
                    in_sync = true;
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

int ReplicationCluster::find_master()
{
    char str[255];
    char master_IP[256];
    int i = 0;
    int found = 0;
    int master_node = 255;
    while ((found == 0) && (i < N))
    {
        if (find_field(nodes[i],
                       (char*) "show slave status;",
                       (char*) "Master_Host",
                       &str[0]
                       ) == 0)
        {
            found = 1;
            strcpy(master_IP, str);
        }
        i++;
    }
    if (found == 1)
    {
        found = 0;
        i = 0;
        while ((found == 0) && (i < N))
        {
            if (strcmp(ip_private(i), master_IP) == 0)
            {
                found = 1;
                master_node = i;
            }
            i++;
        }
    }
    return master_node;
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

int ReplicationCluster::set_repl_user()
{
    int global_result = 0;
    global_result += connect();
    for (int i = 0; i < N; i++)
    {
        global_result += execute_query(nodes[i], "%s", create_repl_user);
    }
    close_connections();
    return global_result;
}

int ReplicationCluster::set_slave(MYSQL* conn, const char* master_host, int master_port)
{
    char str[1024];

    sprintf(str, setup_slave, master_host, master_port);
    if (verbose())
    {
        printf("Setup slave SQL: %s\n", str);
    }
    return execute_query(conn, "%s", str);
}

void ReplicationCluster::replicate_from(int slave, int master, const char* type)
{
    replicate_from(slave, ip_private(master), port[master], type);
}

void ReplicationCluster::replicate_from(int slave, const std::string& host, uint16_t port, const char* type)
{
    std::stringstream change_master;

    change_master << "CHANGE MASTER TO MASTER_HOST = '" << host
                  << "', MASTER_PORT = " << port << ", MASTER_USE_GTID = "
                  << type << ", MASTER_USER='repl', MASTER_PASSWORD='repl';";

    if (verbose())
    {
        std::cout << "Server " << slave + 1
                  << " starting to replicate from " << host << ":" << port << std::endl;
        std::cout << "Query is '" << change_master.str() << "'" << std::endl;
    }

    execute_query(nodes[slave], "STOP SLAVE;");
    execute_query(nodes[slave], "%s", change_master.str().c_str());
    execute_query(nodes[slave], "START SLAVE;");
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
}
