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
#include <vector>

Mariadb_nodes::Mariadb_nodes(const char *pref, const char *test_cwd, bool verbose):
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
    if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) ))
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
                    nodes[i], (char *) "show slave status;",
                    (char *) "Master_Host", &str[0]
                ) == 0 )
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
            execute_query(nodes[i], (char *) "stop slave;");
        }
    }
    execute_query(nodes[NewMaster], create_repl_user);
    execute_query(nodes[OldMaster], (char *) "reset master;");
    find_field(nodes[NewMaster], (char *) "show master status", (char *) "File", &log_file[0]);
    find_field(nodes[NewMaster], (char *) "show master status", (char *) "Position", &log_pos[0]);
    for (i = 0; i < N; i++)
    {
        if (i != NewMaster)
        {
            sprintf(str, setup_slave, IP[NewMaster], log_file, log_pos, port[NewMaster]);
            execute_query(nodes[i], str);
        }
    }
    //for (i = 0; i < N; i++) {if (i != NewMaster) {execute_query(nodes[i], (char *) "start slave;"); }}
}

int Mariadb_nodes::stop_node(int node)
{
    return ssh_node(node, stop_db_command[node], true);
}

int Mariadb_nodes::start_node(int node, char * param)
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
    int i;
    int local_result = 0;
    connect();
    for (i = 0; i < N; i++)
    {
        printf("Stopping node %d\n", i);
        fflush(stdout);
        local_result += execute_query(nodes[i], (char *) "stop slave;");
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
        global_result += execute_query(nodes[i], (char *) "stop slave;");
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



int Mariadb_nodes::start_replication()
{
    char str[1024];
    char dtr[1024];
    int local_result = 0;

    // Start all nodes
    for (int i = 0; i < N; i++)
    {
        local_result += start_node(i, (char *) "");
        sprintf(str,
                "mysql -u root %s -e \"STOP SLAVE; RESET SLAVE; RESET SLAVE ALL; RESET MASTER; SET GLOBAL read_only=OFF;\"",
                socket_cmd[i]);
        ssh_node(i, str, true);
    }

    sprintf(str, "%s/create_user.sh", test_dir);
    sprintf(dtr, "%s", access_homedir[0]);
    copy_to_node(str, dtr , 0);
    sprintf(str, "export node_user=\"%s\"; export node_password=\"%s\"; %s/create_user.sh %s",
            user_name, password, access_homedir[0], socket_cmd[0]);
    printf("cmd: %s\n", str);
    ssh_node(0, str, false);

    // Create a database dump from the master and distribute it to the slaves
    sprintf(str,
            "mysqldump --all-databases --add-drop-database --flush-privileges --master-data=1 --gtid %s > /tmp/master_backup.sql",
            socket_cmd[0]);
    ssh_node(0, str, true);
    sprintf(str, "%s/master_backup.sql", test_dir);
    copy_from_node("/tmp/master_backup.sql", str, 0);

    for (int i = 1; i < N; i++)
    {
        // Reset all nodes by first loading the dump and then starting the replication
        printf("Starting node %d\n", i);
        fflush(stdout);
        copy_to_node(str, "/tmp/master_backup.sql", i);
        sprintf(dtr,
                "mysql -u root %s < /tmp/master_backup.sql",
                socket_cmd[i]);
        ssh_node(i, dtr, true);
        char query[512];

        sprintf(query, "mysql -u root %s -e \"CHANGE MASTER TO MASTER_HOST=\\\"%s\\\", MASTER_PORT=%d, "
                "MASTER_USER=\\\"repl\\\", MASTER_PASSWORD=\\\"repl\\\";"
                "START SLAVE;\"", socket_cmd[i], IP_private[0], port[0]);
        ssh_node(i, query, true);
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
    ssh_node(0, "rm -f /var/lib/mysql/grastate.dat", true);

    printf("Starting new Galera cluster\n");
    fflush(stdout);
    ssh_node(0, "echo [mysqld] > cluster_address.cnf", false);
    ssh_node(0, "echo wsrep_cluster_address=gcomm:// >>  cluster_address.cnf", false);
    ssh_node(0, "cp cluster_address.cnf /etc/my.cnf.d/", true);
    local_result += start_node(0, (char *) " --wsrep-cluster-address=gcomm://");

    sprintf(str, "%s/create_user_galera.sh", test_dir);
    copy_to_node(str, "~/", 0);

    sprintf(str, "export galera_user=\"%s\"; export galera_password=\"%s\"; ./create_user_galera.sh %s", user_name,
            password, socket_cmd[0]);
    ssh_node(0, str, false);

    for (i = 1; i < N; i++)
    {
        printf("Starting node %d\n", i);
        fflush(stdout);
        ssh_node(i, "echo [mysqld] > cluster_address.cnf", true);
        sprintf(str, "echo wsrep_cluster_address=gcomm://%s >>  cluster_address.cnf", IP_private[0]);
        ssh_node(i, str, true);
        ssh_node(i, "cp cluster_address.cnf /etc/my.cnf.d/", true);

        sprintf(&sys1[0], " --wsrep-cluster-address=gcomm://%s", IP_private[0]);
        if (this->verbose)
        {
            printf("%s\n", sys1);
            fflush(stdout);
        }
        local_result += start_node(i, sys1);
        fflush(stdout);
    }
    sleep(5);

    local_result += connect();
    local_result += execute_query(nodes[0], create_repl_user);

    close_connections();
    return local_result;
}

int Mariadb_nodes::clean_iptables(int node)
{
    char sys1[1024];
    int local_result = 0;

    local_result += ssh_node(node, (char *) "echo \"#!/bin/bash\" > clean_iptables.sh", false);
    sprintf(sys1,
            "echo \"while [ \\\"\\$(iptables -n -L INPUT 1|grep '%d')\\\" != \\\"\\\" ]; do iptables -D INPUT 1; done\" >>  clean_iptables.sh",
            port[node]);
    local_result += ssh_node(node, (char *) sys1, false);
    sprintf(sys1,
            "echo \"while [ \\\"\\$(ip6tables -n -L INPUT 1|grep '%d')\\\" != \\\"\\\" ]; do ip6tables -D INPUT 1; done\" >>  clean_iptables.sh",
            port[node]);
    local_result += ssh_node(node, (char *) sys1, false);

    local_result += ssh_node(node, (char *) "chmod a+x clean_iptables.sh", false);
    local_result += ssh_node(node, (char *) "./clean_iptables.sh", true);
    return local_result;
}

int Mariadb_nodes::block_node(int node)
{
    char sys1[1024];
    int local_result = 0;
    local_result += clean_iptables(node);
    sprintf(&sys1[0], "iptables -I INPUT -p tcp --dport %d -j REJECT", port[node]);
    if (this->verbose)
    {
        printf("%s\n", sys1);
        fflush(stdout);
    }
    local_result += ssh_node(node, sys1, true);

    sprintf(&sys1[0], "ip6tables -I INPUT -p tcp --dport %d -j REJECT", port[node]);
    if (this->verbose)
    {
        printf("%s\n", sys1);
        fflush(stdout);
    }
    local_result += ssh_node(node, sys1, true);

    blocked[node] = true;
    return local_result;
}

int Mariadb_nodes::unblock_node(int node)
{
    char sys1[1024];
    int local_result = 0;
    local_result += clean_iptables(node);
    sprintf(&sys1[0], "iptables -I INPUT -p tcp --dport %d -j ACCEPT", port[node]);
    if (this->verbose)
    {
        printf("%s\n", sys1);
        fflush(stdout);
    }
    local_result += ssh_node(node, sys1, true);
    sprintf(&sys1[0], "ip6tables -I INPUT -p tcp --dport %d -j ACCEPT", port[node]);
    if (this->verbose)
    {
        printf("%s\n", sys1);
        fflush(stdout);
    }
    local_result += ssh_node(node, sys1, true);

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

    if (ssh_node(0, (char *) "ls > /dev/null", false) != 0)
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

            if (find_field(conn, (char *) "SHOW STATUS WHERE Variable_name='wsrep_cluster_size';", (char *) "Value",
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

int Mariadb_nodes::ssh_node(int node, const char *ssh, bool sudo)
{
    char sys[strlen(ssh) + 1024];
    generate_ssh_cmd(sys, node, ssh, sudo);
    int return_code = system(sys);
    if (WIFEXITED(return_code))
    {
        return WEXITSTATUS(return_code);
    }
    else
    {
        return 256;
    }
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

        if (mysql_query(nodes[i], "SELECT CONCAT('\\'', user, '\\'@\\'', host, '\\'') FROM mysql.user WHERE user = ''") == 0)
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

                    for (auto& s: users)
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
        if ((local_result += find_field(nodes[i], (char *) "SELECT @@version", (char *) "@@version", version[i])))
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
        if (strstr(version_major[i], "5.") ==  version_major[i])
        {
            version_major[i][3] = 0;
        }
        if (strstr(version_major[i], "10.") ==  version_major[i])
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
        if (strcmp(IP[node], "127.0.0.1") !=0)
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
        local_result += copy_to_node(str, (char *) "~/", i);
        sprintf(str, "%s/ssl.cnf", test_dir);
        local_result += copy_to_node(str, (char *) "~/", i);
        local_result += ssh_node(i, (char *) "cp ~/ssl.cnf /etc/my.cnf.d/", true);
        local_result += ssh_node(i, (char *) "cp -r ~/ssl-cert /etc/", true);
        local_result += ssh_node(i,  (char *) "chown mysql:mysql -R /etc/ssl-cert", true);
        start_node(i,  (char *) "");
    }

    if (require)
    {
        // Create DB user on first node
        printf("Set user to require ssl: %s\n", str);
        sprintf(str, "%s/create_user_ssl.sh", test_dir);
        copy_to_node(str, (char *) "~/", 0);

        sprintf(str, "export node_user=\"%s\"; export node_password=\"%s\"; ./create_user_ssl.sh %s",
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
    sprintf(str, "DROP USER %s;  grant all privileges on *.*  to '%s'@'%%' identified by '%s';", user_name,
            user_name, password);
    local_result += execute_query(nodes[0], (char *) "");
    close_connections();

    for (int i = 0; i < N; i++)
    {
        stop_node(i);
        local_result += ssh_node(i, (char *) "rm -f /etc/my.cnf.d/ssl.cnf", true);
        start_node(i,  (char *) "");
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
        MYSQL_RES *res = mysql_store_result(this->nodes[node]);

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
