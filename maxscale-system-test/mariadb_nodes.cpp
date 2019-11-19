/**
 * @file mariadb_nodes.cpp - backend nodes routines
 *
 * @verbatim
 * Revision History
 *
 * Date     Who     Description
 * 17/11/14 Timofey Turenko Initial implementation
 *
 * @endverbatim
 */

#include "mariadb_nodes.h"
#include "sql_const.h"
#include <climits>
#include <string>
#include <sstream>
#include <iostream>
#include <vector>
#include <future>
#include <functional>
#include <algorithm>
#include "envv.h"

using std::cout;
using std::endl;

namespace
{
static bool g_require_gtid = false;
}

void Mariadb_nodes::require_gtid(bool value)
{
    g_require_gtid = value;
}

Mariadb_nodes::Mariadb_nodes(const char *pref, const char *test_cwd, bool verbose,
                             std::string network_config):
    v51(false)
{
    use_ipv6 = false;
    strcpy(prefix, pref);
    memset(this->nodes, 0, sizeof(this->nodes));
    memset(blocked, 0, sizeof(blocked));
    no_set_pos = false;
    this->verbose = verbose;
    this->network_config = network_config;
    strcpy(test_dir, test_cwd);
    read_env();
    truncate_mariadb_logs();
    flush_hosts();
    close_active_connections();
    cnf_server_name = std::string(prefix);
    if (strcmp(prefix, "node") == 0)
    {
        cnf_server_name = std::string("server");
    }
    if (strcmp(prefix, "galera") == 0)
    {
        cnf_server_name = std::string("gserver");
    }
}

Mariadb_nodes::~Mariadb_nodes()
{
    for (int i = 0; i < N; i++)
    {
        if (blocked[i])
        {
            unblock_node(i);
        }
    }
}

int Mariadb_nodes::connect(int i, const std::string& db)
{
    if (nodes[i] == NULL || mysql_ping(nodes[i]) != 0)
    {
        if (nodes[i])
        {
            mysql_close(nodes[i]);
        }
        nodes[i] = open_conn_db_timeout(port[i], IP[i], db.c_str(), user_name, password, 50, ssl);
    }

    if ((nodes[i] != NULL) && (mysql_errno(nodes[i]) != 0))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

int Mariadb_nodes::connect(const std::string& db)
{
    int res = 0;

    for (int i = 0; i < N; i++)
    {
        res += connect(i, db);
    }

    return res;
}

bool Mariadb_nodes::robust_connect(int n)
{
    bool rval = false;

    for (int i = 0; i < n; i++)
    {
        if (connect() == 0)
        {
            // Connected successfully, return immediately
            rval = true;
            break;
        }

        // We failed to connect, disconnect and wait for a second before trying again
        disconnect();
        sleep(1);
    }

    return rval;
}

void Mariadb_nodes::close_connections()
{
    for (int i = 0; i < N; i++)
    {
        if (nodes[i] != NULL)
        {
            mysql_close(nodes[i]);
            nodes[i] = NULL;
        }
    }
}

void Mariadb_nodes::read_env()
{
    char env_name[64];

    read_basic_env();

    sprintf(env_name, "%s_user", prefix);
    user_name = readenv(env_name, "skysql");

    sprintf(env_name, "%s_password", prefix);
    password = readenv(env_name, "skysql");

    sprintf(env_name, "%s_ssl", prefix);
    ssl = readenv_bool(env_name, false);

    if ((N > 0) && (N < 255))
    {
        for (int i = 0; i < N; i++)
        {
            // reading ports
            sprintf(env_name, "%s_%03d_port", prefix, i);
            port[i] = readenv_int(env_name, 3306);

            //reading sockets
            sprintf(env_name, "%s_%03d_socket", prefix, i);
            socket[i] = readenv(env_name, " ");
            if (strcmp(socket[i], " "))
            {
                socket_cmd[i] = (char *) malloc(strlen(socket[i]) + 10);
                sprintf(socket_cmd[i], "--socket=%s", socket[i]);
            }
            else
            {
                socket_cmd[i] = (char *) " ";
            }
            sprintf(env_name, "%s_%03d_socket_cmd", prefix, i);
            setenv(env_name, socket_cmd[i], 1);

            // reading start_db_command
            sprintf(env_name, "%s_%03d_start_db_command", prefix, i);
            start_db_command[i] = readenv(env_name, (char *) "systemctl start mariadb || service mysql start");

            // reading stop_db_command
            sprintf(env_name, "%s_%03d_stop_db_command", prefix, i);
            stop_db_command[i] = readenv(env_name, (char *) "systemctl stop mariadb || service mysql stop");

            // reading cleanup_db_command
            sprintf(env_name, "%s_%03d_cleanup_db_command", prefix, i);
            cleanup_db_command[i] = readenv(env_name, (char *) "rm -rf /var/lib/mysql/*; killall -9 mysqld");
        }
    }
}

void Mariadb_nodes::print_env()
{
    for (int i = 0; i < N; i++)
    {
        printf("%s node %d \t%s\tPort=%d\n", prefix, i, IP[i], port[i]);
        printf("%s Access user %s\n", prefix, access_user[i]);
    }
    printf("%s User name %s\n", prefix, user_name);
    printf("%s Password %s\n", prefix, password);
}

int Mariadb_nodes::find_master()
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
            if (strcmp(IP[i], master_IP) == 0)
            {
                found = 1;
                master_node = i;
            }
            i++;
        }
    }
    return master_node;
}

void Mariadb_nodes::change_master(int NewMaster, int OldMaster)
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
            sprintf(str, setup_slave, IP[NewMaster], log_file, log_pos, port[NewMaster]);
            execute_query(nodes[i], "%s", str);
        }
    }
}

int Mariadb_nodes::stop_node(int node)
{
    return ssh_node(node, stop_db_command[node], true);
}

int Mariadb_nodes::start_node(int node, const char* param)
{
    char cmd[1024];
    if (v51)
    {
        sprintf(cmd, "%s %s --report-host", start_db_command[node], param);
    }
    else
    {
        sprintf(cmd, "%s %s", start_db_command[node], param);
    }
    return ssh_node(node, cmd, true);
}

int Mariadb_nodes::stop_nodes()
{
    std::vector<std::thread> workers;
    int local_result = 0;
    connect();

    for (int i = 0; i < N; i++)
    {
        workers.emplace_back([&, i]() {
                                 local_result += stop_node(i);
                             });
    }

    for (auto& a : workers)
    {
        a.join();
    }

    return local_result;
}

int Mariadb_nodes::stop_slaves()
{
    int i;
    int global_result = 0;
    connect();
    for (i = 0; i < N; i++)
    {
        printf("Stopping slave %d\n", i);
        fflush(stdout);
        global_result += execute_query(nodes[i], (char*) "stop slave;");
    }
    close_connections();
    return global_result;
}

int Mariadb_nodes::cleanup_db_node(int node)
{
    return ssh_node(node, cleanup_db_command[node], true);
}

int Mariadb_nodes::cleanup_db_nodes()
{
    int i;
    int local_result = 0;

    for (i = 0; i < N; i++)
    {
        printf("Cleaning node %d\n", i);
        fflush(stdout);
        local_result += cleanup_db_node(i);
        fflush(stdout);
    }
    return local_result;
}

void Mariadb_nodes::create_users(int node)
{
    char str[strlen(test_dir) + 17];
    // Create users for replication as well as the users that are used by the tests
    sprintf(str, "%s/create_user.sh", test_dir);
    copy_to_node(node, str, access_homedir[node]);
    ssh_node_f(node, true,
               "export node_user=\"%s\"; export node_password=\"%s\"; %s/create_user.sh %s",
               user_name, password, access_homedir[0], socket_cmd[0]);
}

int Mariadb_nodes::start_replication()
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

        if (g_require_gtid)
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
                          IP_private[0],
                          port[0],
                          g_require_gtid ?
                          "MASTER_USE_GTID=slave_pos" :
                          "MASTER_LOG_FILE='mar-bin.000001', MASTER_LOG_POS=4");

            execute_query(nodes[i], "START SLAVE");
        }
    }

    disconnect();

    return local_result;
}

int Galera_nodes::start_galera()
{
    bool old_verbose = verbose;
    int local_result = 0;
    local_result += stop_nodes();

    std::stringstream ss;

    for (int i = 0; i < N; i++)
    {
        ss << (i == 0 ? "" : ",") << IP_private[i];
    }

    auto gcomm = ss.str();

    for (int i = 0; i < N; i++)
    {
        // Remove the grastate.dat file
        ssh_node(i, "rm -f /var/lib/mysql/grastate.dat", true);

        ssh_node(i, "echo [mysqld] > cluster_address.cnf", true);
        ssh_node_f(i, true, "echo wsrep_cluster_address=gcomm://%s >>  cluster_address.cnf", gcomm.c_str());
        ssh_node(i, "cp cluster_address.cnf /etc/my.cnf.d/", true);

        ssh_node_f(i,
                   true,
                   "sed -i 's/###NODE-ADDRESS###/%s/' /etc/my.cnf.d/* /etc/mysql/my.cnf.d/*;"
                   "sed -i \"s|###GALERA-LIB-PATH###|$(ls /usr/lib*/galera/*.so)|g\" /etc/my.cnf.d/* /etc/mysql/my.cnf.d/*",
                   IP[i]);
    }

    printf("Starting new Galera cluster\n");
    fflush(stdout);

    // Start the first node that also starts a new cluster
    ssh_node_f(0, true, "galera_new_cluster");

    for (int i = 0; i < N; i++)
    {
        if (start_node(i, "") != 0)
        {
            cout << "Failed to start node" << i << endl;
            cout << "---------- BEGIN LOGS ----------" << endl;
            verbose = true;
            ssh_node_f(0, true, "sudo journalctl -u mariadb | tail -n 50");
            cout << "----------- END LOGS -----------" << endl;
        }
    }

    char str[strlen(test_dir) + 25];
    sprintf(str, "%s/create_user_galera.sh", test_dir);
    copy_to_node_legacy(str, "~/", 0);

    ssh_node_f(0, true, "export galera_user=\"%s\"; export galera_password=\"%s\"; ./create_user_galera.sh %s",
               user_name,
               password,
               socket_cmd[0]);

    local_result += robust_connect(5) ? 0 : 1;
    local_result += execute_query(nodes[0], "%s", create_repl_user);

    close_connections();
    verbose = old_verbose;
    return local_result;
}

int Mariadb_nodes::clean_iptables(int node)
{
    return ssh_node_f(node, true,
                      "while [ \"$(iptables -n -L INPUT 1|grep '%d')\" != \"\" ]; do iptables -D INPUT 1; done;"
                      "while [ \"$(ip6tables -n -L INPUT 1|grep '%d')\" != \"\" ]; do ip6tables -D INPUT 1; done;",
                      port[node], port[node]);
}

int Mariadb_nodes::block_node(int node)
{
    int local_result = 0;

    local_result += ssh_node_f(node, true,
                               "iptables -I INPUT -p tcp --dport %d -j REJECT;"
                               "ip6tables -I INPUT -p tcp --dport %d -j REJECT",
                               port[node], port[node]);
    blocked[node] = true;
    return local_result;
}

int Mariadb_nodes::unblock_node(int node)
{
    int local_result = 0;
    local_result += clean_iptables(node);
    local_result += ssh_node_f(node, true,
                               "iptables -I INPUT -p tcp --dport %d -j ACCEPT;"
                               "ip6tables -I INPUT -p tcp --dport %d -j ACCEPT",
                               port[node], port[node]);

    blocked[node] = false;
    return local_result;
}


int Mariadb_nodes::unblock_all_nodes()
{
    int rval = 0;
    std::vector<std::thread> threads;

    for (int i = 0; i < this->N; i++)
    {
        threads.emplace_back([&, i]() {
                                 rval += this->unblock_node(i);
                             });
    }

    for (auto& a : threads)
    {
        a.join();
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

bool Mariadb_nodes::check_master_node(MYSQL* conn)
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
bool Mariadb_nodes::bad_slave_thread_status(MYSQL* conn, const char* field, int node)
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

        if (verbose)
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
        if (verbose)
        {
            printf("Node %d: %s is '%s'\n", node, field, str);
        }
        rval = true;
    }

    return rval;
}

static bool wrong_replication_type(MYSQL* conn)
{
    bool rval = true;

    for (int i = 0; i < 2; i++)
    {
        char str[1024] = "";

        if (find_field(conn, "SHOW SLAVE STATUS", "Gtid_IO_Pos", str) == 0)
        {
            // If the test requires GTID based replication, Gtid_IO_Pos must not be empty
            if ((rval = (*str != '\0') != g_require_gtid))
            {
                printf("Wrong value for 'Gtid_IO_Pos' (%s), expected it to be %s.\n",
                       str,
                       g_require_gtid ? "not empty" : "empty");
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

/**
 * @brief multi_source_replication Check if slave is connected to more then one master
 * @param conn MYSQL struct (have to be open)
 * @param node Node index
 * @return false if multisource replication is not detected
 */
static bool multi_source_replication(MYSQL* conn, int node)
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

int Mariadb_nodes::check_replication()
{
    int master = 0;
    int res = 0;

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

    if ((res = get_versions()) != 0)
    {
        cout << "Failed to get versions" << endl;
    }

    for (int i = 0; i < N && res == 0; i++)
    {
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
        printf("Replication check for %s gave code %d\n", prefix, res);
    }

    return res;
}

bool Mariadb_nodes::fix_replication()
{
    bool rval = true;

    if (check_replication())
    {
        cout << prefix << ": Replication is broken, fixing..." << endl;
        rval = false;

        if (unblock_all_nodes() == 0)
        {
            cout << "Prepare nodes" << endl;
            prepare_servers();
            cout << "Starting replication" << endl;
            start_replication();

            if (check_replication() == 0)
            {
                cout << "Replication is fixed" << endl;
                flush_hosts();
                rval = true;
            }
            else
            {
                cout << "FATAL ERROR: Replication is still broken" << endl;
            }
        }
        else
        {
            cout << "SSH access to nodes doesn't work" << endl;
        }
    }

    return rval;
}

bool Mariadb_nodes::revert_nodes_snapshot()
{
    char str[1024];
    bool rval = true;
    for (int i = 0; i < N; i++)
    {
        sprintf(str, "%s clean --node-name %s_%03d", revert_snapshot_command, prefix, i);
        if (system(str))
        {
            rval = false;
        }
        ssh_node_f(i, true, "pkill -9 mysqld");
    }
    return rval;
}

int Galera_nodes::check_galera()
{
    int res = 1;

    if (verbose)
    {
        printf("Checking Galera\n");
        fflush(stdout);
    }

    if (connect() == 0)
    {
        Row r = get_row(nodes[0], "SHOW STATUS WHERE Variable_name='wsrep_cluster_size'");

        if (r.size() == 2)
        {
            if (r[1] == std::to_string(N))
            {
                res = 0;
            }
            else
            {
                cout << "Expected cluster size: " << N << " Actual size: " << r[1] << endl;
            }
        }
        else
        {
            cout << "Unexpected result size: "
                 << (r.empty() ? "Empty result" : std::to_string(r.size())) << endl;
        }
    }
    else
    {
        cout << "Failed to connect to the cluster" << endl;
    }

    disconnect();

    return res;
}

int Mariadb_nodes::set_slave(MYSQL* conn,
                             char   master_host[],
                             int master_port,
                             char log_file[],
                             char log_pos[])
{
    char str[1024];

    sprintf(str, setup_slave, master_host, log_file, log_pos, master_port);
    if (no_set_pos)
    {
        sprintf(str, setup_slave_no_pos, master_host, master_port);
    }

    if (this->verbose)
    {
        printf("Setup slave SQL: %s\n", str);
    }
    return execute_query(conn, "%s", str);
}

int Mariadb_nodes::set_repl_user()
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

int Mariadb_nodes::get_server_id(int index)
{
    int id = -1;
    char str[1024];

    if (find_field(this->nodes[index], "SELECT @@server_id", "@@server_id", (char*) str) == 0)
    {
        id = atoi(str);
    }
    else
    {
        printf("find_field failed for %s:%d\n", this->IP[index], this->port[index]);
    }

    return id;
}

std::string Mariadb_nodes::get_server_id_str(int index)
{
    std::stringstream ss;
    ss << get_server_id(index);
    return ss.str();
}

std::vector<int> Mariadb_nodes::get_all_server_ids()
{
    std::vector<int> rval;

    for (int i = 0; i < N; i++)
    {
        rval.push_back(get_server_id(i));
    }

    return rval;
}

bool do_flush_hosts(MYSQL* conn)
{
    int local_result = 0;

    if (mysql_query(conn, "FLUSH HOSTS"))
    {
        local_result++;
    }

    if (mysql_query(conn, "SET GLOBAL max_connections=10000"))
    {
        local_result++;
    }

    if (mysql_query(conn, "SET GLOBAL max_connect_errors=10000000"))
    {
        local_result++;
    }

    if (mysql_query(conn,
                    "SELECT CONCAT('\\'', user, '\\'@\\'', host, '\\'') FROM mysql.user WHERE user = ''")
        == 0)
    {
        MYSQL_RES* res = mysql_store_result(conn);

        if (res)
        {
            std::vector<std::string> users;
            MYSQL_ROW row;

            while ((row = mysql_fetch_row(res)))
            {
                users.push_back(row[0]);
            }

            mysql_free_result(res);

            if (users.size() > 0)
            {
                printf("Detected anonymous users, dropping them.\n");

                for (auto& s : users)
                {
                    std::string query = "DROP USER ";
                    query += s;
                    printf("%s\n", query.c_str());
                    mysql_query(conn, query.c_str());
                }
            }
        }
    }
    else
    {
        printf("Failed to query for anonymous users: %s\n", mysql_error(conn));
        local_result++;
    }

    return local_result == 0;
}

int Mariadb_nodes::flush_hosts()
{

    if (this->nodes[0] == NULL && this->connect())
    {
        return 1;
    }

    bool all_ok = true;
    std::vector<std::future<bool>> futures;

    for (int i = 0; i < N; i++)
    {
        std::packaged_task<bool(MYSQL*)> task(do_flush_hosts);
        futures.push_back(task.get_future());
        std::thread(std::move(task), nodes[i]).detach();
    }

    for (auto& f : futures)
    {
        f.wait();
        if (!f.get())
        {
            all_ok = false;
        }
    }

    return all_ok;
}

int Mariadb_nodes::execute_query_all_nodes(const char* sql)
{
    int local_result = 0;
    connect();
    for (int i = 0; i < N; i++)
    {
        local_result += execute_query(nodes[i], "%s", sql);
    }
    close_connections();
    return local_result;
}

int Mariadb_nodes::get_version(int i)
{
    char* str;
    int ec;
    int local_result = 0;
    if (find_field(nodes[i], "SELECT @@version", "@@version", version[i]))
    {
        cout << "Failed to get version: " << mysql_error(nodes[i]) << ", trying ssh node and use MariaDB client" << endl;
        str = ssh_node_output(i, "mysql --batch --silent  -e \"select @@version\"", true, &ec);
        if (ec)
        {
            local_result++;
            cout << "Failed to get version, node " << i << " is broken" << endl;
        }
        else
        {
            strcpy(version[i], str);
            free(str);
        }
    }
    strcpy(version_number[i], version[i]);
    str = strchr(version_number[i], '-');
    if (str != NULL)
    {
        str[0] = 0;
    }
    strcpy(version_major[i], version_number[i]);
    if (strstr(version_major[i], "5.") == version_major[i])
    {
        version_major[i][3] = 0;
    }
    if (strstr(version_major[i], "10.") == version_major[i])
    {
        version_major[i][4] = 0;
    }

    if (verbose)
    {
        printf("Node %s%d: %s\t %s \t %s\n", prefix, i, version[i], version_number[i], version_major[i]);
    }
    return local_result;
}

int Mariadb_nodes::get_versions()
{
    int local_result = 0;

    v51 = false;

    for (int i = 0; i < N; i++)
    {
        local_result += get_version(i);
    }

    for (int i = 0; i < N; i++)
    {
        if (strcmp(version_major[i], "5.1") == 0)
        {
            v51 = true;
        }
    }

    return local_result;
}

std::string Mariadb_nodes::get_lowest_version()
{
    std::string rval;
    get_versions();

    int lowest = INT_MAX;

    for (int i = 0; i < N; i++)
    {
        int int_version = get_int_version(version[i]);

        if (lowest > int_version)
        {
            rval = version[i];
            lowest = int_version;
        }
    }

    return rval;
}

int Mariadb_nodes::truncate_mariadb_logs()
{
    std::vector<std::future<int>> results;

    for (int node = 0; node < N; node++)
    {
        if (strcmp(IP[node], "127.0.0.1") != 0)
        {
            auto f = std::async(std::launch::async, &Nodes::ssh_node_f, this, node, true,
                                "truncate -s 0 /var/lib/mysql/*.err;"
                                "truncate -s 0 /var/log/syslog;"
                                "truncate -s 0 /var/log/messages;"
                                "rm -f /etc/my.cnf.d/binlog_enc*;");
            results.push_back(std::move(f));
        }
    }

    return std::count_if(results.begin(), results.end(), std::mem_fn(&std::future<int>::get));
}

int Mariadb_nodes::configure_ssl(bool require)
{
    int local_result = 0;
    char str[strlen(test_dir) + 20];

    this->ssl = 1;

    for (int i = 0; i < N; i++)
    {
        printf("Node %d\n", i);
        stop_node(i);
        sprintf(str, "%s/ssl-cert", test_dir);
        local_result += copy_to_node_legacy(str, (char*) "~/", i);
        sprintf(str, "%s/ssl.cnf", test_dir);
        local_result += copy_to_node_legacy(str, (char*) "~/", i);
        local_result += ssh_node(i, (char*) "cp ~/ssl.cnf /etc/my.cnf.d/", true);
        local_result += ssh_node(i, (char*) "cp -r ~/ssl-cert /etc/", true);
        local_result += ssh_node(i, (char*) "chown mysql:mysql -R /etc/ssl-cert", true);
        start_node(i, (char*) "");
    }

    if (require)
    {
        // Create DB user on first node
        printf("Set user to require ssl: %s\n", str);
        sprintf(str, "%s/create_user_ssl.sh", test_dir);
        copy_to_node_legacy(str, (char*) "~/", 0);

        sprintf(str,
                "export node_user=\"%s\"; export node_password=\"%s\"; ./create_user_ssl.sh %s",
                user_name,
                password,
                socket_cmd[0]);
        printf("cmd: %s\n", str);
        ssh_node(0, str, false);
    }

    return local_result;
}

int Mariadb_nodes::disable_ssl()
{
    int local_result = 0;
    char str[1024];

    local_result += connect();
    sprintf(str,
            "DROP USER %s;  grant all privileges on *.*  to '%s'@'%%' identified by '%s';",
            user_name,
            user_name,
            password);
    local_result += execute_query(nodes[0], "%s", "");
    close_connections();

    for (int i = 0; i < N; i++)
    {
        stop_node(i);
        local_result += ssh_node(i, (char*) "rm -f /etc/my.cnf.d/ssl.cnf", true);
        start_node(i, (char*) "");
    }

    return local_result;
}

static void wait_until_pos(MYSQL* mysql, int filenum, int pos)
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

void Mariadb_nodes::sync_slaves(int node)
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

void Mariadb_nodes::close_active_connections()
{
    if (this->nodes[0] == NULL)
    {
        this->connect();
    }

    const char* sql
        =
            "select id from information_schema.processlist where id != @@pseudo_thread_id and user not in ('system user', 'repl')";

    for (int i = 0; i < N; i++)
    {
        if (!mysql_query(nodes[i], sql))
        {
            MYSQL_RES* res = mysql_store_result(nodes[i]);
            if (res)
            {
                MYSQL_ROW row;

                while ((row = mysql_fetch_row(res)))
                {
                    std::string q("KILL ");
                    q += row[0];
                    execute_query_silent(nodes[i], q.c_str());
                }
                mysql_free_result(res);
            }
        }
    }
}


void Mariadb_nodes::stash_server_settings(int node)
{
    ssh_node(node, "sudo rm -rf /etc/my.cnf.d.backup/", true);
    ssh_node(node, "sudo mkdir /etc/my.cnf.d.backup/", true);
    ssh_node(node, "sudo cp -r /etc/my.cnf.d/* /etc/my.cnf.d.backup/", true);
}

void Mariadb_nodes::restore_server_settings(int node)
{
    ssh_node(node, "sudo mv -f /etc/my.cnf.d.backup/* /etc/my.cnf.d/", true);
}

void Mariadb_nodes::disable_server_setting(int node, const char* setting)
{
    ssh_node_f(node, true, "sudo sed -i 's/%s/#%s/' /etc/my.cnf.d/*", setting, setting);
}

void Mariadb_nodes::add_server_setting(int node, const char* setting)
{
    ssh_node_f(node, true, "sudo sed -i '$a [server]' /etc/my.cnf.d/*server*.cnf");
    ssh_node_f(node, true, "sudo sed -i '$a %s' /etc/my.cnf.d/*server*.cnf", setting);
}

std::string Mariadb_nodes::get_config_name(int node)
{
    std::stringstream ss;
    ss << "server" << node + 1 << ".cnf";
    return ss.str();
}

std::string Galera_nodes::get_config_name(int node)
{
    std::stringstream ss;
    ss << "galera_server" << node + 1 << ".cnf";
    return ss.str();
}

void Mariadb_nodes::reset_server_settings(int node)
{
    std::string cnfdir = std::string(test_dir) + "/mdbci/cnf/";
    std::string cnf = get_config_name(node);

    // Note: This is a CentOS specific path
    ssh_node(node, "rm -rf /etc/my.cnf.d/*", true);
    copy_to_node(node, (cnfdir + cnf).c_str(), "~/");
    ssh_node_f(node, false, "sudo install -o root -g root -m 0644 ~/%s /etc/my.cnf.d/", cnf.c_str());
}

void Mariadb_nodes::reset_server_settings()
{
    for (int node = 0; node < N; node++)
    {
        reset_server_settings(node);
    }
}

/**
 * @brief extract_version_from_string Tries to find MariaDB server version number in the output of 'mysqld
 *--version'
 * Function does not allocate any memory
 * @param version String returned by 'mysqld --version'
 * @return pointer to the string with version number
 */
char* extract_version_from_string(char* version)
{
    int pos1 = 0;
    int pos2 = 0;
    int l = strlen(version);
    while ((!isdigit(version[pos1])) && (pos1 < l))
    {
        pos1++;
    }
    pos2 = pos1;
    while (((isdigit(version[pos2]) || version[pos2] == '.')) && (pos2 < l))
    {
        pos2++;
    }
    version[pos2] = '\0';
    return &version[pos1];
}

int Mariadb_nodes::prepare_server(int i)
{
    cleanup_db_node(i);
    reset_server_settings(i);

    // Note: These should be done by MDBCI
    ssh_node(i, "test -d /etc/apparmor.d/ && "
                "ln -s /etc/apparmor.d/usr.sbin.mysqld /etc/apparmor.d/disable/usr.sbin.mysqld && "
                "sudo service apparmor restart && "
                "chmod a+r -R /etc/my.cnf.d/*", true);

    int ec;
    char* version = ssh_node_output(i, "/usr/sbin/mysqld --version", false, &ec);

    if (ec == 0)
    {
        char* version_digits;
        char* tmp_pass;
        version_digits = extract_version_from_string(version);
        printf("Detected server version on node %d is %s\n", i, version_digits);

        if (memcmp(version_digits, "5.", 2) == 0)
        {
            ssh_node(i, "sed -i \"s/binlog_row_image=full//\" /etc/my.cnf.d/*.cnf", true);
        }
        if (memcmp(version_digits, "5.7", 3) == 0)
        {
            // Disable 'validate_password' plugin, searach for random temporal
            // password in the log and reseting passord to empty string
            ssh_node(i, "/usr/sbin/mysqld --initialize; sudo chown -R mysql:mysql /var/lib/mysql", true);
            ssh_node(i, start_db_command[i], true);
            tmp_pass = ssh_node_output(i,
                                       "cat /var/log/mysqld.log | grep \"temporary password\" | sed -n -e 's/^.*: //p'",
                                       true,
                                       &ec);
            ssh_node_f(i, true, "mysqladmin -uroot -p'%s' password '%s'", tmp_pass, tmp_pass);
            ssh_node_f(i,
                       false,
                       "echo \"UNINSTALL PLUGIN validate_password\" | sudo mysql -uroot -p'%s'",
                       tmp_pass);
            ssh_node(i, stop_db_command[i], true);
            ssh_node(i, start_db_command[i], true);
            ssh_node_f(i, true, "mysqladmin -uroot -p'%s' password ''", tmp_pass);
        }
        else
        {
            cout << "Executing mysql_install_db on node" << i << endl;
            ssh_node(i, "mysql_install_db; sudo chown -R mysql:mysql /var/lib/mysql", true);
        }
    }

    free(version);
    return ec;
}

int Mariadb_nodes::prepare_servers()
{
    int rval = 0;
    std::vector<std::thread> threads;

    for (int i = 0; i < N; i++)
    {
        threads.emplace_back([&, i]() {
                                 rval += prepare_server(i);
                             });
    }

    for (auto& a : threads)
    {
        a.join();
    }

    return rval;
}

void Mariadb_nodes::replicate_from(int slave, int master, const char* type)
{
    std::stringstream change_master;
    change_master << "CHANGE MASTER TO MASTER_HOST = '" << IP[master]
                  << "', MASTER_PORT = " << port[master] << ", MASTER_USE_GTID = " << type << ", "
                                                                                "MASTER_USER='repl', MASTER_PASSWORD='repl';";

    if (verbose)
    {
        std::cout << "Server " << slave + 1 << " starting to replicate from server " << master + 1
                  << std::endl;
        std::cout << "Query is '" << change_master.str() << "'" << std::endl;
    }

    execute_query(nodes[slave], "STOP SLAVE;");
    execute_query(nodes[slave], "%s", change_master.str().c_str());
    execute_query(nodes[slave], "START SLAVE;");
}

void Mariadb_nodes::limit_nodes(int new_N)
{
    if (N > new_N)
    {
        execute_query_all_nodes((char*) "stop slave;");
        N = new_N;
        fix_replication();
        sleep(10);
    }
}

std::string Mariadb_nodes::cnf_servers()
{
    std::string s;
    for (int i = 0; i < N; i++)
    {
        s += std::string("\\n[") +
                cnf_server_name +
                std::to_string(i + 1) +
                std::string("]\\ntype=server\\naddress=") +
                std::string(IP[i]) +
                std::string("\\nport=") +
                std::to_string(port[i]) +
                std::string("\\nprotocol=MySQLBackend\\n");
    }
    return s;
}

std::string Mariadb_nodes::cnf_servers_line()
{
    std::string s = cnf_server_name + std::to_string(1);
    for (int i = 1; i < N; i++)
    {
        s += std::string(",") +
                cnf_server_name +
                std::to_string(i + 1);
    }
    return s;
}
