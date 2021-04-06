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
#include <maxtest/log.hh>
#include <maxtest/mariadb_connector.hh>
#include <iostream>
#include <maxbase/format.hh>

using std::string;
using std::cout;
using std::endl;

namespace
{
const string type_mariadb = "mariadb";
const char create_repl_user[] =
    "grant replication slave on *.* to repl@'%%' identified by 'repl'; "
    "FLUSH PRIVILEGES";
const char setup_slave[] =
    "change master to MASTER_HOST='%s', "
    "MASTER_USER='repl', "
    "MASTER_PASSWORD='repl', "
    "MASTER_LOG_FILE='%s', "
    "MASTER_LOG_POS=%s, "
    "MASTER_PORT=%d; "
    "start slave;";

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

void wait_until_pos(MYSQL* mysql, int filenum, int pos)
{
    int slave_filenum = 0;
    int slave_pos = 0;

    do
    {
        if (mysql_query(mysql, "SHOW SLAVE STATUS"))
        {
            printf("Failed to execute SHOW SLAVE STATUS: %s", mysql_error(mysql));
            break;
        }

        MYSQL_RES* res = mysql_store_result(mysql);

        if (res)
        {
            MYSQL_ROW row = mysql_fetch_row(res);

            if (row && row[5] && strchr(row[5], '.') && row[21])
            {
                char* file_suffix = strchr(row[5], '.') + 1;
                slave_filenum = atoi(file_suffix);
                slave_pos = atoi(row[21]);
            }
            mysql_free_result(res);
        }
    }
    while (slave_filenum < filenum || slave_pos < pos);
}
}

namespace maxtest
{
ReplicationCluster::ReplicationCluster(SharedData* shared)
    : MariaDBCluster(shared, "node", "server")
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
    int res = true;
    const bool verbose = this->verbose();
    if (verbose)
    {
        logger().log_msgf("Checking Master-Slave cluster.");
    }

    if (connect())
    {
        cout << "Failed to connect to all servers" << endl;
        return false;
    }

    if (!update_status())
    {
        cout << "Failed to update status" << endl;
        return false;
    }

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
                if (!check_master_node(conn_ptr))
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

    if (verbose)
    {
        printf("Replication check for %s gave code %d\n", prefix().c_str(), res);
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
                io_running = res->get_string("Slave_IO_Running");
                sql_running = res->get_string("Slave_SQL_Running");
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

void ReplicationCluster::sync_slaves(int node)
{
    if (this->nodes[node] == NULL)
    {
        this->connect();
    }

    if (mysql_query(this->nodes[node], "SHOW MASTER STATUS"))
    {
        printf("Failed to execute SHOW MASTER STATUS: %s", mysql_error(this->nodes[node]));
    }
    else
    {
        MYSQL_RES* res = mysql_store_result(this->nodes[node]);

        if (res)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[node] && row[1])
            {
                const char* file_suffix = strchr(row[node], '.') + 1;
                int filenum = atoi(file_suffix);
                int pos = atoi(row[1]);

                for (int i = 0; i < this->N; i++)
                {
                    if (i != node)
                    {
                        wait_until_pos(this->nodes[i], filenum, pos);
                    }
                }
            }
            mysql_free_result(res);
        }
    }
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
    char log_file[256];
    char log_pos[256];
    find_field(nodes[NewMaster], "show master status", "File", &log_file[0]);
    find_field(nodes[NewMaster], "show master status", "Position", &log_pos[0]);

    for (int i = 0; i < N; i++)
    {
        if (i != NewMaster && mysql_ping(nodes[i]) == 0)
        {
            char str[1024];
            sprintf(str, setup_slave, ip_private(NewMaster), log_file, log_pos, port[NewMaster]);
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

int ReplicationCluster::set_slave(MYSQL* conn, const char* master_host, int master_port,
                                  const char* log_file, const char* log_pos)
{
    char str[1024];

    sprintf(str, setup_slave, master_host, log_file, log_pos, master_port);
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
}
