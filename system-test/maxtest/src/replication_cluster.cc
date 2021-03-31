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
#include <iostream>

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

/**
 * @brief multi_source_replication Check if slave is connected to more then one master
 * @param conn MYSQL struct (have to be open)
 * @param node Node index
 * @return false if multisource replication is not detected
 */
bool multi_source_replication(MYSQL* conn, int node)
{
    bool rval = true;
    MYSQL_RES* res;

    if (mysql_query(conn, "SHOW ALL SLAVES STATUS") == 0
        && (res = mysql_store_result(conn)))
    {
        if (mysql_num_rows(res) == 1)
        {
            rval = false;
        }
        else
        {
            printf("Node %d: More than one configured slave\n", node);
            fflush(stdout);
        }

        mysql_free_result(res);
    }
    else
    {
        printf("Node %d does not support SHOW ALL SLAVE STATUS, ignoring multi source replication check\n",
               node);
        fflush(stdout);
        rval = false;
    }

    return rval;
}

bool is_readonly(MYSQL* conn)
{
    bool rval = false;
    char output[512];
    find_field(conn, "SHOW VARIABLES LIKE 'read_only'", "Value", output);

    if (strcasecmp(output, "OFF") != 0)
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

    for (int i = 0; i < N; i++)
    {
        execute_query(nodes[i], "SET GLOBAL read_only=OFF");
        execute_query(nodes[i], "STOP SLAVE;");

        bool using_gtid = m_shared.settings.req_mariadb_gtid;
        if (using_gtid)
        {
            execute_query(nodes[i], "SET GLOBAL gtid_slave_pos='0-1-0'");
        }

        if (i != 0)
        {
            // TODO: Reuse the code in sync_slaves() to get the actual file name and position
            execute_query(nodes[i],
                          "CHANGE MASTER TO "
                          "MASTER_HOST='%s', MASTER_PORT=%d, "
                          "MASTER_USER='repl', MASTER_PASSWORD='repl', "
                          "%s",
                          ip_private(0),
                          port[0],
                          using_gtid ?
                          "MASTER_USE_GTID=slave_pos" :
                          "MASTER_LOG_FILE='mar-bin.000001', MASTER_LOG_POS=4");

            execute_query(nodes[i], "START SLAVE");
        }
    }

    disconnect();

    return local_result;
}

int ReplicationCluster::check_replication()
{
    int master = 0;
    int res = 0;

    const bool verbose = this->verbose();
    if (verbose)
    {
        printf("Checking Master/Slave setup\n");
        fflush(stdout);
    }

    if (connect())
    {
        cout << "Failed to connect to all servers" << endl;
        return 1;
    }

    if (!update_status())
    {
        cout << "Failed to update status" << endl;
        return 1;
    }

    for (int i = 0; i < N && res == 0; i++)
    {
        if (ssl && !check_ssl(i))
        {
            res = 1;
        }

        if (mysql_query(nodes[i], "SELECT COUNT(*) FROM mysql.user") == 0)
        {
            mysql_free_result(mysql_store_result(nodes[i]));
        }
        else
        {
            cout << mysql_error(nodes[i]) << endl;
            res = 1;
        }

        if (i == master)
        {
            if (!check_master_node(nodes[i]))
            {
                res = 1;
                if (verbose)
                {
                    printf("Master node check failed for node %d\n", i);
                }
            }
        }
        else if (bad_slave_thread_status(nodes[i], "Slave_IO_Running", i)
                 || bad_slave_thread_status(nodes[i], "Slave_SQL_Running", i)
                 || wrong_replication_type(nodes[i])
                 || multi_source_replication(nodes[i], i)
                 || is_readonly(nodes[i]))
        {
            res = 1;
            if (verbose)
            {
                printf("Slave %d check failed\n", i);
            }
        }
    }

    if (verbose)
    {
        printf("Replication check for %s gave code %d\n", prefix().c_str(), res);
    }

    return res;
}

bool ReplicationCluster::check_master_node(MYSQL* conn)
{
    bool rval = true;

    if (mysql_query(conn, "SHOW SLAVE STATUS"))
    {
        cout << mysql_error(conn) << endl;
        rval = false;
    }
    else
    {
        MYSQL_RES* res = mysql_store_result(conn);

        if (res)
        {
            if (mysql_num_rows(res) > 0)
            {
                cout << "The master is configured as a slave" << endl;
                rval = false;
            }
            mysql_free_result(res);
        }
    }

    if (is_readonly(conn))
    {
        printf("The master is in read-only mode\n");
        rval = false;
    }

    return rval;
}

/**
 * @brief bad_slave_thread_status Check if field in the slave status outpur is not 'yes'
 * @param conn MYSQL struct (connection have to be open)
 * @param field Field to check
 * @param node Node index
 * @return false if requested field is 'Yes'
 */
bool ReplicationCluster::bad_slave_thread_status(MYSQL* conn, const char* field, int node)
{
    char str[1024] = "";
    bool rval = false;

    // Doing 3 attempts to check status
    for (int i = 0; i < 2; i++)
    {
        if (find_field(conn, "SHOW SLAVE STATUS;", field, str) != 0)
        {
            printf("Node %d: %s not found in SHOW SLAVE STATUS\n", node, field);
            break;
        }

        if (verbose())
        {
            printf("Node %d: field %s is %s\n", node, field, str);
        }

        if (strcmp(str, "Yes") == 0 || strcmp(str, "No") == 0)
        {
            break;
        }

        /** Any other state is transient and we should try again */
        sleep(1);
    }

    if (strcmp(str, "Yes") != 0)
    {
        if (verbose())
        {
            printf("Node %d: %s is '%s'\n", node, field, str);
        }
        rval = true;
    }

    return rval;
}

bool ReplicationCluster::wrong_replication_type(MYSQL* conn)
{
    bool rval = true;

    for (int i = 0; i < 2; i++)
    {
        char str[1024] = "";

        if (find_field(conn, "SHOW SLAVE STATUS", "Gtid_IO_Pos", str) == 0)
        {
            bool require_gtid = m_shared.settings.req_mariadb_gtid;
            // If the test requires GTID based replication, Gtid_IO_Pos must not be empty
            if ((rval = (*str != '\0') != require_gtid))
            {
                printf("Wrong value for 'Gtid_IO_Pos' (%s), expected it to be %s.\n",
                       str,
                       require_gtid ? "not empty" : "empty");
            }
            else
            {
                break;
            }
        }
        sleep(1);
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
                  << " starting to replicate from server " << master + 1 << std::endl;
        std::cout << "Query is '" << change_master.str() << "'" << std::endl;
    }

    execute_query(nodes[slave], "STOP SLAVE;");
    execute_query(nodes[slave], "%s", change_master.str().c_str());
    execute_query(nodes[slave], "START SLAVE;");
}
}
