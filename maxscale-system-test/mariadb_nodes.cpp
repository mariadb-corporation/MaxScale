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
#include <vector>

Mariadb_nodes::Mariadb_nodes(const char *pref, const char *test_cwd, bool verbose) :
    v51(false), use_ipv6(false)
{
    strcpy(prefix, pref);
    memset(this->nodes, 0, sizeof(this->nodes));
    memset(blocked, 0, sizeof(blocked));
    no_set_pos = false;
    this->verbose = verbose;
    strcpy(test_dir, test_cwd);
    read_env();
    truncate_mariadb_logs();
    flush_hosts();
    close_active_connections();
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

int Mariadb_nodes::connect()
{
    int res = 0;

    for (int i = 0; i < N; i++)
    {
        if (nodes[i] == NULL || mysql_ping(nodes[i]) != 0)
        {
            if (nodes[i])
            {
                mysql_close(nodes[i]);
            }
            nodes[i] = open_conn_db_timeout(port[i], IP[i], "test", user_name, password, 50, ssl);
        }

        if ((nodes[i] != NULL) && (mysql_errno(nodes[i]) != 0))
        {
            res++;
        }
    }

    return res;
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

int Mariadb_nodes::read_env()
{
    char * env;
    char env_name[64];

    sprintf(env_name, "%s_N", prefix);
    env = getenv(env_name);
    if (env != NULL)
    {
        sscanf(env, "%d", &N);
    }
    else
    {
        N = 0;
    }

    sprintf(env_name, "%s_user", prefix);
    env = getenv(env_name);
    if (env != NULL)
    {
        sscanf(env, "%s", user_name);
    }
    else
    {
        sprintf(user_name, "skysql");
    }
    sprintf(env_name, "%s_password", prefix);
    env = getenv(env_name);
    if (env != NULL)
    {
        sscanf(env, "%s", password);
    }
    else
    {
        sprintf(password, "skysql");
    }

    ssl = false;
    sprintf(env_name, "%s_ssl", prefix);
    env = getenv(env_name);
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0)))
    {
        ssl = true;
    }


    if ((N > 0) && (N < 255))
    {
        for (int i = 0; i < N; i++)
        {
            //reading IPs
            sprintf(env_name, "%s_%03d_network", prefix, i);
            env = getenv(env_name);
            if (env != NULL)
            {
                sprintf(IP[i], "%s", env);
            }

            //reading private IPs
            sprintf(env_name, "%s_%03d_private_ip", prefix, i);
            env = getenv(env_name);
            if (env != NULL)
            {
                sprintf(IP_private[i], "%s", env);
            }
            else
            {
                sprintf(IP_private[i], "%s", IP[i]);
            }

            //reading IPv6
            sprintf(env_name, "%s_%03d_network6", prefix, i);
            env = getenv(env_name);
            if (env != NULL)
            {
                sprintf(IP6[i], "%s", env);
            }
            else
            {
                sprintf(IP6[i], "%s", IP[i]);
            }

            //reading ports
            sprintf(env_name, "%s_%03d_port", prefix, i);
            env = getenv(env_name);
            if (env != NULL)
            {
                sscanf(env, "%d", &port[i]);
            }
            else
            {
                port[i] = 3306;
            }
            //reading sockets
            sprintf(env_name, "%s_%03d_socket", prefix, i);
            env = getenv(env_name);
            if (env != NULL)
            {
                sprintf(socket[i], "%s", env);
                sprintf(socket_cmd[i], "--socket=%s", env);
            }
            else
            {
                sprintf(socket[i], " ");
                sprintf(socket_cmd[i], " ");
            }
            //reading sshkey
            sprintf(env_name, "%s_%03d_keyfile", prefix, i);
            env = getenv(env_name);
            if (env != NULL)
            {
                sprintf(sshkey[i], "%s", env);
            }
            else
            {
                sprintf(sshkey[i], "vagrant.pem");
            }

            //reading start_db_command
            sprintf(env_name, "%s_%03d_start_db_command", prefix, i);
            env = getenv(env_name);
            if (env != NULL)
            {
                sprintf(start_db_command[i], "%s", env);
            }
            else
            {
                sprintf(start_db_command[i], "%s", "service mysql start");
            }

            //reading stop_db_command
            sprintf(env_name, "%s_%03d_stop_db_command", prefix, i);
            env = getenv(env_name);
            if (env != NULL)
            {
                sprintf(stop_db_command[i], "%s", env);
            }
            else
            {
                sprintf(start_db_command[i], "%s", "service mysql stop");
            }

            //reading cleanup_db_command
            sprintf(env_name, "%s_%03d_cleanup_db_command", prefix, i);
            env = getenv(env_name);
            if (env != NULL)
            {
                sprintf(cleanup_db_command[i], "%s", env);
            }
            else
            {
                sprintf(cleanup_db_command[i], " ");
            }

            sprintf(env_name, "%s_%03d_whoami", prefix, i);
            env = getenv(env_name);
            if (env != NULL)
            {
                sprintf(access_user[i], "%s", env);
            }
            else
            {
                sprintf(access_user[i], "root");
            }

            sprintf(env_name, "%s_%03d_access_sudo", prefix, i);
            env = getenv(env_name);
            if (env != NULL)
            {
                sprintf(access_sudo[i], "%s", env);
            }
            else
            {
                sprintf(access_sudo[i], " ");
            }

            if (strcmp(access_user[i], "root") == 0)
            {
                sprintf(access_homedir[i], "/%s/", access_user[i]);
            }
            else
            {
                sprintf(access_homedir[i], "/home/%s/", access_user[i]);
            }

        }
    }
}

int Mariadb_nodes::print_env()
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
        if (find_field(
                nodes[i], "show slave status;",
                "Master_Host", &str[0]
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

int Mariadb_nodes::change_master(int NewMaster, int OldMaster)
{
    int i;
    //int OldMaster = FindMaster();
    char log_file[256];
    char log_pos[256];
    char str[1024];

    for (i = 0; i < N; i++)
    {
        if (i != OldMaster)
        {
            execute_query(nodes[i], "stop slave;");
        }
    }
    execute_query(nodes[NewMaster], create_repl_user);
    execute_query(nodes[OldMaster], "reset master;");
    find_field(nodes[NewMaster], "show master status", "File", &log_file[0]);
    find_field(nodes[NewMaster], "show master status", "Position", &log_pos[0]);
    for (i = 0; i < N; i++)
    {
        if (i != NewMaster)
        {
            sprintf(str, setup_slave, IP[NewMaster], log_file, log_pos, port[NewMaster]);
            execute_query(nodes[i], str);
        }
    }
    //for (i = 0; i < N; i++) {if (i != NewMaster) {execute_query(nodes[i], "start slave;"); }}
}

int Mariadb_nodes::stop_node(int node)
{
    return ssh_node(node, true, stop_db_command[node]);
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
    return ssh_node(node, true, cmd);
}

int Mariadb_nodes::stop_nodes()
{
    int i;
    int local_result = 0;
    connect();
    for (i = 0; i < N; i++)
    {
        printf("Stopping node %d\n", i);
        fflush(stdout);
        local_result += execute_query(nodes[i], "stop slave;");
        fflush(stdout);
        local_result += stop_node(i);
        fflush(stdout);
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
        global_result += execute_query(nodes[i], "stop slave;");
    }
    close_connections();
    return global_result;
}

int Mariadb_nodes::cleanup_db_node(int node)
{
    return ssh_node(node, true, cleanup_db_command[node]);
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

int Mariadb_nodes::start_replication()
{
    char str[1024];
    char dtr[1024];
    int local_result = 0;

    // Start all nodes
    for (int i = 0; i < N; i++)
    {
        local_result += start_node(i, "");
        ssh_node(i, true,
                 "mysql -u root %s -e \"STOP SLAVE; RESET SLAVE; RESET SLAVE ALL; RESET MASTER; SET GLOBAL read_only=OFF;\"",
                 socket_cmd[i]);
        ssh_node(i, true, "sudo rm -f /etc/my.cnf.d/kerb.cnf");
        ssh_node(i, true,
                 "for i in `mysql  -ss -u root %s -e \"SHOW DATABASES\"|grep -iv 'mysql\\|information_schema\\|performance_schema'`; "
                 "do mysql -u root %s -e \"DROP DATABASE $i\";"
                 "done", socket_cmd[i], socket_cmd[i]);
    }

    sprintf(str, "%s/create_user.sh", test_dir);
    sprintf(dtr, "%s", access_homedir[0]);
    copy_to_node(str, dtr, 0);
    ssh_node(0, false, "export node_user=\"%s\"; export node_password=\"%s\"; %s/create_user.sh %s",
             user_name, password, access_homedir[0], socket_cmd[0]);

    // Create a database dump from the master and distribute it to the slaves
    ssh_node(0, true, "mysql -u root %s -e \"CREATE DATABASE test\"; "
             "mysqldump --all-databases --add-drop-database --flush-privileges --master-data=1 --gtid %s > /tmp/master_backup.sql",
             socket_cmd[0], socket_cmd[0]);
    sprintf(str, "%s/master_backup.sql", test_dir);
    copy_from_node("/tmp/master_backup.sql", str, 0);

    for (int i = 1; i < N; i++)
    {
        // Reset all nodes by first loading the dump and then starting the replication
        printf("Starting node %d\n", i);
        fflush(stdout);
        copy_to_node(str, "/tmp/master_backup.sql", i);
        ssh_node(i, true, "mysql -u root %s < /tmp/master_backup.sql",
                 socket_cmd[i]);
        ssh_node(i, true, "mysql -u root %s -e \"CHANGE MASTER TO MASTER_HOST=\\\"%s\\\", MASTER_PORT=%d, "
                 "MASTER_USER=\\\"repl\\\", MASTER_PASSWORD=\\\"repl\\\";"
                 "START SLAVE;\"", socket_cmd[i], IP_private[0], port[0]);
    }

    return local_result;
}

int Galera_nodes::start_galera()
{
    char sys1[4096];
    char str[1024];
    int i;
    int local_result = 0;
    local_result += stop_nodes();

    // Remove the grastate.dat file
    ssh_node(0, true, "rm -f /var/lib/mysql/grastate.dat");

    printf("Starting new Galera cluster\n");
    fflush(stdout);
    ssh_node(0, false, "echo [mysqld] > cluster_address.cnf");
    ssh_node(0, false, "echo wsrep_cluster_address=gcomm:// >>  cluster_address.cnf");
    ssh_node(0, true, "cp cluster_address.cnf /etc/my.cnf.d/");
    local_result += start_node(0, " --wsrep-cluster-address=gcomm://");

    sprintf(str, "%s/create_user_galera.sh", test_dir);
    copy_to_node(str, "~/", 0);

    ssh_node(0, false, "export galera_user=\"%s\"; export galera_password=\"%s\"; ./create_user_galera.sh %s",
             user_name,
             password, socket_cmd[0]);

    for (i = 1; i < N; i++)
    {
        printf("Starting node %d\n", i);
        ssh_node(i, true, "echo [mysqld] > cluster_address.cnf");
        ssh_node(i, true, "echo wsrep_cluster_address=gcomm://%s >>  cluster_address.cnf", IP_private[0]);
        ssh_node(i, true, "cp cluster_address.cnf /etc/my.cnf.d/");
        sprintf(str, " --wsrep-cluster-address=gcomm://%s", IP_private[0]);
        local_result += start_node(i, str);
    }
    sleep(5);

    local_result += connect();
    local_result += execute_query(nodes[0], create_repl_user);

    close_connections();
    return local_result;
}

int Mariadb_nodes::clean_iptables(int node)
{
    int local_result = 0;

    local_result += ssh_node(node, false, "echo \"#!/bin/bash\" > clean_iptables.sh");
    local_result += ssh_node(node, false,
                             "echo \"while [ \\\"\\$(iptables -n -L INPUT 1|grep '%d')\\\" != \\\"\\\" ]; "
                             "do iptables -D INPUT 1; done\" >>  clean_iptables.sh",
                             port[node]);
    local_result += ssh_node(node, false,
                             "echo \"while [ \\\"\\$(ip6tables -n -L INPUT 1|grep '%d')\\\" != \\\"\\\" ]; "
                             "do ip6tables -D INPUT 1; done\" >>  clean_iptables.sh",
                             port[node]);

    local_result += ssh_node(node, false, "chmod a+x clean_iptables.sh");
    local_result += ssh_node(node, true, "./clean_iptables.sh");
    return local_result;
}

int Mariadb_nodes::block_node(int node)
{
    int local_result = 0;
    local_result += clean_iptables(node);

    local_result += ssh_node(node, true, "iptables -I INPUT -p tcp --dport %d -j REJECT", port[node]);
    local_result += ssh_node(node, true, "ip6tables -I INPUT -p tcp --dport %d -j REJECT", port[node]);

    blocked[node] = true;
    return local_result;
}

int Mariadb_nodes::unblock_node(int node)
{
    int local_result = 0;
    local_result += clean_iptables(node);

    local_result += ssh_node(node, true, "iptables -I INPUT -p tcp --dport %d -j ACCEPT", port[node]);
    local_result += ssh_node(node, true, "ip6tables -I INPUT -p tcp --dport %d -j ACCEPT", port[node]);

    blocked[node] = false;
    return local_result;
}

int Mariadb_nodes::unblock_all_nodes()
{
    int rval = 0;
    for (int i = 0; i < this->N; i++)
    {
        rval += this->unblock_node(i);
    }
    return rval;
}

int Mariadb_nodes::check_node_ssh(int node)
{
    int res = 0;
    printf("Checking node %d\n", node);
    fflush(stdout);

    if (ssh_node(0, false, "ls > /dev/null") != 0)
    {
        printf("Node %d is not available\n", node);
        fflush(stdout);
        res = 1;
    }
    else
    {
        printf("Node %d is OK\n", node);
        fflush(stdout);
    }
    return res;
}

int Mariadb_nodes::check_nodes()
{
    int res = 0;
    for (int i = 0; i < N; i++)
    {
        res += check_node_ssh(i);
    }
    return res;
}

bool Mariadb_nodes::check_master_node(MYSQL *conn)
{
    bool rval = true;

    if (mysql_query(conn, "SHOW SLAVE HOSTS"))
    {
        printf("%s\n", mysql_error(conn));
        rval = false;
    }
    else
    {
        MYSQL_RES *res = mysql_store_result(conn);

        if (res)
        {
            int rows = mysql_num_rows(res);

            if (rows != N - 1)
            {
                if (!v51)
                {
                    printf("Number of slave hosts is %d when it should be %d\n", rows, N - 1);
                    rval = false;
                }
            }
        }
        mysql_free_result(res);
    }

    if (mysql_query(conn, "SHOW SLAVE STATUS"))
    {
        printf("%s\n", mysql_error(conn));
        rval = false;
    }
    else
    {
        MYSQL_RES *res = mysql_store_result(conn);

        if (res)
        {
            if (mysql_num_rows(res) > 0)
            {
                printf("The master is configured as a slave\n");
                rval = false;
            }
            mysql_free_result(res);
        }
    }

    char output[512];
    find_field(conn, "SHOW VARIABLES LIKE 'read_only'", "Value", output);

    if (strcmp(output, "OFF"))
    {
        printf("The master is in read-only mode\n");
        rval = false;
    }

    return rval;
}

static bool bad_slave_thread_status(MYSQL *conn, const char *field, int node)
{
    char str[1024] = "";
    bool rval = false;

    for (int i = 0; i < 2; i++)
    {
        if (find_field(conn, "SHOW SLAVE STATUS;", field, str) != 0)
        {
            printf("Node %d: %s not found in SHOW SLAVE STATUS\n", node, field);
            fflush(stdout);
            break;
        }
        else if (strcmp(str, "Yes") == 0 || strcmp(str, "No") == 0)
        {
            break;
        }

        /** Any other state is transient and we should try again */
        sleep(1);
    }

    if (strcmp(str, "Yes") != 0)
    {
        printf("Node %d: %s is '%s'\n", node, field, str);
        fflush(stdout);
        rval = true;
    }
    return rval;
}

int Mariadb_nodes::check_replication()
{
    int master = 0;
    int res = 0;
    char str[1024];

    if (verbose)
    {
        printf("Checking Master/Slave setup\n");
        fflush(stdout);
    }

    if (this->connect())
    {
        return 1;
    }

    res = get_versions();

    for (int i = 0; i < N && res == 0; i++)
    {
        if (i == master)
        {
            if (!check_master_node(nodes[i]))
            {
                res = 1;
            }
        }
        else if (bad_slave_thread_status(nodes[i], "Slave_IO_Running", i) ||
                 bad_slave_thread_status(nodes[i], "Slave_SQL_Running", i))
        {
            res = 1;
        }
    }

    return res;
}

bool Mariadb_nodes::fix_replication()
{
    if (check_replication())
    {
        unblock_all_nodes();

        if (check_nodes())
        {
            printf("****** VMS ARE BROKEN! Exiting *****\n");
            return false;
        }

        int attempts = 2;
        int attempts_with_cleanup = 2;

        while (check_replication() && attempts > 0)
        {
            if (attempts != 2)
            {
                stop_nodes();
            }

            start_replication();
            close_connections();

            attempts--;

            if (attempts == 0 && check_replication())
            {
                if (attempts_with_cleanup > 0)
                {
                    printf("****** BACKEND IS STILL BROKEN! Trying to cleanup all nodes *****\n");
                    stop_nodes();
                    cleanup_db_nodes();
                    attempts_with_cleanup--;
                    attempts = 2;
                    sleep(30);
                    start_replication();
                    sleep(30);
                }
                else
                {
                    printf("****** BACKEND IS STILL BROKEN! Exiting *****\n");
                    return false;
                }
            }
        }
        flush_hosts();
    }

    return true;
}

int Galera_nodes::check_galera()
{
    int res1 = 0;

    if (verbose)
    {
        printf("Checking Galera\n");
        fflush(stdout);
    }

    if (this->nodes[0] == NULL)
    {
        this->connect();
    }

    res1 = get_versions();

    for (int i = 0; i < N; i++)
    {
        MYSQL *conn = open_conn(port[i], IP[i], user_name, password, ssl);
        if (conn == NULL || mysql_errno(conn) != 0)
        {
            printf("Error connectiong node %d: %s\n", i, mysql_error(conn));
            res1 = 1;
        }
        else
        {
            char str[1024] = "";

            if (find_field(conn, "SHOW STATUS WHERE Variable_name='wsrep_cluster_size';", "Value",
                           str) != 0)
            {
                printf("wsrep_cluster_size is not found in SHOW STATUS LIKE 'wsrep%%' results\n");
                fflush(stdout);
                res1 = 1;
            }
            else
            {
                int cluster_size;
                sscanf(str, "%d", &cluster_size);
                if (cluster_size != N)
                {
                    printf("wsrep_cluster_size is not %d, it is %d\n", N, cluster_size);
                    fflush(stdout);
                    res1 = 1;
                }
            }
        }
        mysql_close(conn);
    }

    return res1;
}

int Mariadb_nodes::set_slave(MYSQL * conn, char master_host[], int master_port, char log_file[],
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
    return execute_query(conn, str);
}

int Mariadb_nodes::set_repl_user()
{
    int global_result = 0;
    global_result += connect();
    for (int i = 0; i < N; i++)
    {
        global_result += execute_query(nodes[i], create_repl_user);
    }
    close_connections();
    return global_result;
}

int Mariadb_nodes::get_server_id(int index)
{
    int id = -1;
    char str[1024];

    if (find_field(this->nodes[index], "SELECT @@server_id", "@@server_id", (char*)str) == 0)
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

void Mariadb_nodes::generate_ssh_cmd(char *cmd, int node, const char *ssh, bool sudo)
{
    if (strcmp(IP[node], "127.0.0.1") == 0)
    {
        if (sudo)
        {
            sprintf(cmd, "%s %s",
                    access_sudo[node], ssh);
        }
        else
        {
            sprintf(cmd, "%s",
                    ssh);

        }
    }
    else
    {

        if (sudo)
        {
            sprintf(cmd,
                    "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet %s@%s '%s %s\'",
                    sshkey[node], access_user[node], IP[node], access_sudo[node], ssh);
        }
        else
        {
            sprintf(cmd,
                    "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet %s@%s '%s'",
                    sshkey[node], access_user[node], IP[node], ssh);
        }
    }
}

char * Mariadb_nodes::ssh_node_output(int node, const char *ssh, bool sudo, int *exit_code)
{
    char sys[strlen(ssh) + 1024];
    generate_ssh_cmd(sys, node, ssh, sudo);
    FILE *output = popen(sys, "r");
    if (output == NULL)
    {
        printf("Error opening ssh %s\n", strerror(errno));
        return NULL;
    }
    char buffer[1024];
    size_t rsize = sizeof(buffer);
    char* result = (char*)calloc(rsize, sizeof(char));

    while (fgets(buffer, sizeof(buffer), output))
    {
        result = (char*)realloc(result, sizeof(buffer) + rsize);
        rsize += sizeof(buffer);
        strcat(result, buffer);
    }
    int code = pclose(output);
    if (WIFEXITED(code))
    {
        * exit_code = WEXITSTATUS(code);
    }
    else
    {
        * exit_code = 256;
    }
    return result;
}

int Mariadb_nodes::ssh_node(int node, bool sudo, const char *format, ...)
{
    va_list valist;

    va_start(valist, format);
    int message_len = vsnprintf(NULL, 0, format, valist);
    va_end(valist);

    if (message_len < 0)
    {
        return -1;
    }

    char *sys = (char*)malloc(message_len + 1);

    va_start(valist, format);
    vsnprintf(sys, message_len + 1, format, valist);
    va_end(valist);

    char *cmd = (char*)malloc(message_len + 1024);

    if (strcmp(IP[node], "127.0.0.1") == 0)
    {
        sprintf(cmd, "bash");
    }
    else
    {
        sprintf(cmd,
                "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet %s@%s > /dev/null",
                sshkey[node], access_user[node], IP[node]);
    }
    int rc = 1;
    FILE *in = popen(cmd, "w");

    if (in)
    {
        if (sudo)
        {
            fprintf(in, "sudo su -\n");
            fprintf(in, "cd /home/%s\n", access_user[node]);
        }

        fprintf(in, "%s\n", sys);
        rc = pclose(in);
    }

    free(sys);
    free(cmd);
    return rc;
}

int Mariadb_nodes::flush_hosts()
{

    if (this->nodes[0] == NULL && this->connect())
    {
        return 1;
    }

    int local_result = 0;

    for (int i = 0; i < N; i++)
    {
        if (mysql_query(nodes[i], "FLUSH HOSTS"))
        {
            local_result++;
        }

        if (mysql_query(nodes[i], "SET GLOBAL max_connections=10000"))
        {
            local_result++;
        }

        if (mysql_query(nodes[i],
                        "SELECT CONCAT('\\'', user, '\\'@\\'', host, '\\'') FROM mysql.user WHERE user = ''") == 0)
        {
            MYSQL_RES *res = mysql_store_result(nodes[i]);

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
                        mysql_query(nodes[i], query.c_str());
                    }
                }
            }
        }
        else
        {
            printf("Failed to query for anonymous users: %s\n", mysql_error(nodes[i]));
            local_result++;
        }
    }

    return local_result;
}

int Mariadb_nodes::execute_query_all_nodes(const char* sql)
{
    int local_result = 0;
    connect();
    for (int i = 0; i < N; i++)
    {
        local_result += execute_query(nodes[i], sql);
    }
    close_connections();
    return local_result;
}

int Mariadb_nodes::get_versions()
{
    int local_result = 0;
    char * str;
    v51 = false;

    for (int i = 0; i < N; i++)
    {
        if ((local_result += find_field(nodes[i], "SELECT @@version", "@@version", version[i])))
        {
            printf("Failed to get version: %s\n", mysql_error(nodes[i]));
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
    int local_result = 0;
    for (int node = 0; node < N; node++)
    {
        char sys[1024];
        if (strcmp(IP[node], "127.0.0.1") != 0)
        {
            sprintf(sys,
                    "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet %s@%s 'sudo truncate  /var/lib/mysql/*.err --size 0;sudo rm -f /etc/my.cnf.d/binlog_enc*\' &",
                    sshkey[node], access_user[node], IP[node]);
            local_result += system(sys);
        }
    }
    return local_result;
}

int Mariadb_nodes::configure_ssl(bool require)
{
    int local_result = 0;
    char str[1024];

    this->ssl = 1;

    for (int i = 0; i < N; i++)
    {
        printf("Node %d\n", i);
        stop_node(i);
        sprintf(str, "%s/ssl-cert", test_dir);
        local_result += copy_to_node(str, "~/", i);
        sprintf(str, "%s/ssl.cnf", test_dir);
        local_result += copy_to_node(str, "~/", i);
        local_result += ssh_node(i, true, "cp ~/ssl.cnf /etc/my.cnf.d/");
        local_result += ssh_node(i, true, "cp -r ~/ssl-cert /etc/");
        local_result += ssh_node(i, true, "chown mysql:mysql -R /etc/ssl-cert");
        start_node(i, "");
    }

    if (require)
    {
        // Create DB user on first node
        printf("Set user to require ssl: %s\n", str);
        sprintf(str, "%s/create_user_ssl.sh", test_dir);
        copy_to_node(str, "~/", 0);
        ssh_node(0, false, "export node_user=\"%s\"; export node_password=\"%s\"; "
                 "./create_user_ssl.sh %s", user_name, password, socket_cmd[0]);
    }

    return local_result;
}

int Mariadb_nodes::disable_ssl()
{
    int local_result = 0;
    char str[1024];

    local_result += connect();
    sprintf(str, "DROP USER %s;  grant all privileges on *.*  to '%s'@'%%' identified by '%s';", user_name,
            user_name, password);
    local_result += execute_query(nodes[0], "");
    close_connections();

    for (int i = 0; i < N; i++)
    {
        stop_node(i);
        local_result += ssh_node(i, true, "rm -f /etc/my.cnf.d/ssl.cnf");
        start_node(i, "");
    }

    return local_result;
}

int Mariadb_nodes::copy_to_node(const char* src, const char* dest, int i)
{
    if (i >= N)
    {
        return 1;
    }
    char sys[strlen(src) + strlen(dest) + 1024];

    if (strcmp(IP[i], "127.0.0.1") == 0)
    {
        sprintf(sys, "cp %s %s",
                src, dest);
    }
    else
    {
        sprintf(sys, "scp -q -r -i %s -o UserKnownHostsFile=/dev/null "
                "-o StrictHostKeyChecking=no -o LogLevel=quiet %s %s@%s:%s",
                sshkey[i], src, access_user[i], IP[i], dest);
    }
    if (verbose)
    {
        printf("%s\n", sys);
    }

    return system(sys);
}

int Mariadb_nodes::copy_from_node(const char* src, const char* dest, int i)
{
    if (i >= N)
    {
        return 1;
    }
    char sys[strlen(src) + strlen(dest) + 1024];
    if (strcmp(IP[i], "127.0.0.1") == 0)
    {
        sprintf(sys, "cp %s %s",
                src, dest);
    }
    else
    {
        sprintf(sys, "scp -q -r -i %s -o UserKnownHostsFile=/dev/null "
                "-o StrictHostKeyChecking=no -o LogLevel=quiet %s@%s:%s %s",
                sshkey[i], access_user[i], IP[i], src, dest);
    }
    if (verbose)
    {
        printf("%s\n", sys);
    }

    return system(sys);
}

static void wait_until_pos(MYSQL *mysql, int filenum, int pos)
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

        MYSQL_RES *res = mysql_store_result(mysql);

        if (res)
        {
            MYSQL_ROW row = mysql_fetch_row(res);

            if (row && row[6] && row[21])
            {
                char *file_suffix = strchr(row[5], '.') + 1;
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
    if (this->nodes[node] == NULL && this->connect())
    {
        printf("Failed to connect to all nodes.\n");
        return;
    }

    if (mysql_query(this->nodes[node], "SHOW MASTER STATUS"))
    {
        printf("Failed to execute SHOW MASTER STATUS: %s\n", mysql_error(this->nodes[node]));
    }
    else
    {
        MYSQL_RES *res = mysql_store_result(this->nodes[node]);

        if (res)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0] && row[1])
            {
                const char* file_suffix = strchr(row[0], '.');
                if (file_suffix)
                {
                    file_suffix++;
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
                else
                {
                    printf("Cannot sync slaves, invalid binlog file name: %s\n", row[0]);
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

    const char *sql =
        "select id from information_schema.processlist where id != @@pseudo_thread_id and user not in ('system user', 'repl')";

    for (int i = 0; i < N; i++)
    {
        if (!mysql_query(nodes[i], sql))
        {
            MYSQL_RES *res = mysql_store_result(nodes[i]);
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
