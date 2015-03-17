
/**
 * @file mariadb_nodes.cpp - backend nodes routines
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 17/11/14	Timofey Turenko	Initial implementation
 *
 * @endverbatim
 */

#include "mariadb_nodes.h"
#include "sql_const.h"

Mariadb_nodes::Mariadb_nodes(char * pref)
{
    strcpy(prefix, pref);
}

int Mariadb_nodes::connect()
{
    for (int i = 0; i < N; i++) {
        nodes[i] = open_conn(port[i], IP[i], user_name, password);
    }
}

int Mariadb_nodes::close_connections()
{
    for (int i = 0; i < N; i++) {
        if (nodes[i] != NULL) {mysql_close(nodes[i]);}
    }
}

int Mariadb_nodes::read_env()
{
    char * env;
    char env_name[64];

    sprintf(env_name, "%s_N", prefix);
    env = getenv(env_name); if (env != NULL) {sscanf(env, "%d", &N); } else {N = 0;}

    sprintf(env_name, "%s_user", prefix);
    env = getenv(env_name); if (env != NULL) {sscanf(env, "%s", user_name); } else {sprintf(user_name, "skysql"); }
    sprintf(env_name, "%s_password", prefix);
    env = getenv(env_name); if (env != NULL) {sscanf(env, "%s", password); } else {sprintf(password, "skysql"); }


    sprintf(env_name, "%s_kill_vm_command", prefix);
    env = getenv(env_name); if (env != NULL) {sscanf(env, "%s", kill_vm_command); } else {sprintf(kill_vm_command, "exit 1"); }

    sprintf(env_name, "%s_start_vm_command", prefix);
    env = getenv(env_name); if (env != NULL) {sscanf(env, "%s", start_vm_command); } else {sprintf(start_vm_command, "exit 1"); }


    if ((N > 0) && (N < 255)) {
        for (int i = 0; i < N; i++) {
            //reading IPs
            sprintf(env_name, "%s_%03d", prefix, i);
            env = getenv(env_name); if (env != NULL) {sprintf(IP[i], "%s", env);}

            //reading ports
            sprintf(env_name, "%s_port_%03d", prefix, i);
            env = getenv(env_name); if (env != NULL) {
                sscanf(env, "%d", &port[i]);
            } else {
                port[i] = 3306;
            }
            //reading sshkey
            sprintf(env_name, "%s_sshkey_%03d", prefix, i);
            env = getenv(env_name); if (env != NULL) {sprintf(sshkey[i], "%s", env);}


        }
    }
}

int Mariadb_nodes::print_env()
{
    for (int i = 0; i < N; i++) {printf("%s node %d \t%s\tPort=%d\n", prefix, i, IP[i], port[i]);}
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
    while ((found == 0) && (i < N)) {
        if (find_status_field(
                    nodes[i], (char *) "show slave status;",
                    (char *) "Master_Host", &str[0]
                    ) == 0 ) {
            found = 1;
            strcpy(master_IP, str);
        }
        i++;
    }
    if (found == 1) {
        found = 0; i = 0;
        while ((found == 0) && (i < N)) {
            if (strcmp(IP[i], master_IP) == 0) {
                found = 1;
                master_node = i;
            }
            i++;
        }
    }
    return(master_node);
}

int Mariadb_nodes::change_master(int NewMaster, int OldMaster)
{
    int i;
    //int OldMaster = FindMaster();
    char log_file[256];
    char log_pos[256];
    char str[1024];

    for (i = 0; i < N; i++) {
        if (i != OldMaster) {execute_query(nodes[i], (char *) "stop slave;");}
    }
    execute_query(nodes[NewMaster], create_repl_user);
    execute_query(nodes[OldMaster], (char *) "reset master;");
    find_status_field(nodes[NewMaster], (char *) "show master status", (char *) "File", &log_file[0]);
    find_status_field(nodes[NewMaster], (char *) "show master status", (char *) "Position", &log_pos[0]);
    for (i = 0; i < N; i++) {
        if (i != NewMaster) {
            sprintf(str, setup_slave, IP[NewMaster], log_file, log_pos, port[NewMaster]);
            execute_query(nodes[i], str);
        }
    }
    //for (i = 0; i < N; i++) {if (i != NewMaster) {execute_query(nodes[i], (char *) "start slave;"); }}
}

int Mariadb_nodes::stop_nodes()
{
    int i;
    int global_result = 0;
    char sys1[4096];
    connect();
    for (i = 0; i < N; i++) {
        printf("Stopping slave %d\n", i); fflush(stdout);
        global_result += execute_query(nodes[i], (char *) "stop slave;");
        printf("Stopping %d\n", i); fflush(stdout);
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql stop'", sshkey[i], IP[i]);
        printf("%s\n", sys1);  fflush(stdout);
        global_result += system(sys1); fflush(stdout);
    }
    return(global_result);
}

int Mariadb_nodes::stop_slaves()
{
    int i;
    int global_result = 0;
    connect();
    for (i = 0; i < N; i++) {
        printf("Stopping slave %d\n", i); fflush(stdout);
        global_result += execute_query(nodes[i], (char *) "stop slave;");
    }
    return(global_result);
}

int Mariadb_nodes::start_replication()
{
    char sys1[4096];
    char str[1024];
    char log_file[256];
    char log_pos[256];
    int i;
    int global_result = 0;
    global_result += stop_nodes();

    printf("Starting back Master\n");  fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql start'", sshkey[0], IP[0]);
    printf("%s\n", sys1);  fflush(stdout);
    global_result +=  system(sys1); fflush(stdout);

    for (i = 1; i < N; i++) {
        printf("Starting node %d\n", i); fflush(stdout);
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql start '", sshkey[i], IP[i]);
        printf("%s\n", sys1);  fflush(stdout);
        global_result += system(sys1); fflush(stdout);
    }
    sleep(5);

    global_result += connect();
    global_result += execute_query(nodes[0], create_repl_user);
    execute_query(nodes[0], (char *) "reset master;");

    find_status_field(nodes[0], (char *) "show master status", (char *) "File", &log_file[0]);
    find_status_field(nodes[0], (char *) "show master status", (char *) "Position", &log_pos[0]);
    for (i = 1; i < N; i++) {
        global_result += execute_query(nodes[i], (char *) "stop slave;");
        sprintf(str, setup_slave, IP[0], log_file, log_pos, port[0]);
        global_result += execute_query(nodes[i], str);
    }
    close_connections();
    return(global_result);
}

int Mariadb_nodes::start_galera()
{
    char sys1[4096];
    int i;
    int global_result = 0;
    global_result += stop_nodes();

    printf("Starting new Galera cluster\n");  fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql start --wsrep-cluster-address=gcomm://'", sshkey[0], IP[0]);
    printf("%s\n", sys1);  fflush(stdout);
    global_result +=  system(sys1); fflush(stdout);

    for (i = 1; i < N; i++) {
        printf("Starting node %d\n", i); fflush(stdout);
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql start --wsrep-cluster-address=gcomm://%s'", sshkey[i], IP[i], IP[0]);
        printf("%s\n", sys1);  fflush(stdout);
        global_result += system(sys1); fflush(stdout);
    }
    sleep(5);

    global_result += connect();
    global_result += execute_query(nodes[0], create_repl_user);

    close_connections();
    return(global_result);
}


int Mariadb_nodes::start_binlog(char * Maxscale_IP, int binlog_port)
{
    char sys1[4096];
    char str[1024];
    char log_file[256];
    char log_pos[256];
    int i;
    int global_result = 0;
    printf("Stopping all backend nodes\n");fflush(stdout);
    global_result += stop_nodes();


    printf("Starting back Master\n");  fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql start --log-bin  --binlog-checksum=CRC32'", sshkey[0], IP[0]);
    printf("%s\n", sys1);  fflush(stdout);
    global_result +=  system(sys1); fflush(stdout);

    for (i = 1; i < N; i++) {
        printf("Starting node %d\n", i); fflush(stdout);
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql start --log-bin  --binlog-checksum=CRC32'", sshkey[i], IP[i]);
        printf("%s\n", sys1);  fflush(stdout);
        global_result += system(sys1); fflush(stdout);
    }
    sleep(5);

    printf("Connecting to all backend nodes\n");fflush(stdout);
    global_result += connect();
    printf("Creating repl user\n");fflush(stdout);
    global_result += execute_query(nodes[0], create_repl_user);
    printf("'reset master' query to node 0\n");fflush(stdout);
    execute_query(nodes[0], (char *) "reset master;");

    printf("show master status\n");fflush(stdout);
    find_status_field(nodes[0], (char *) "show master status", (char *) "File", &log_file[0]);
    find_status_field(nodes[0], (char *) "show master status", (char *) "Position", &log_pos[0]);
    printf("Real master file: %s\n", log_file); fflush(stdout);
    printf("Real master pos : %s\n", log_pos); fflush(stdout);

    printf("Stopping first slave (node 1)\n");fflush(stdout);
    global_result += execute_query(nodes[1], (char *) "stop slave;");
    printf("Configure first backend slave node to be slave of real master\n");fflush(stdout);
    sprintf(str, setup_slave, IP[0], log_file, log_pos, port[0]);
    global_result += execute_query(nodes[1], str);

    printf("Connecting to MaxScale binlog router\n");fflush(stdout);
    MYSQL * binlog = open_conn(binlog_port, Maxscale_IP, user_name, password);

    printf("show master status\n");fflush(stdout);
    find_status_field(binlog, (char *) "show master status", (char *) "File", &log_file[0]);
    find_status_field(binlog, (char *) "show master status", (char *) "Position", &log_pos[0]);

    printf("Maxscale binlog master file: %s\n", log_file); fflush(stdout);
    printf("Maxscale binlog master pos : %s\n", log_pos); fflush(stdout);

    printf("Setup all backend nodes except first one to be slaves of binlog Maxscale node\n");fflush(stdout);
    for (i = 2; i < N; i++) {
        global_result += execute_query(nodes[i], (char *) "stop slave;");

         //sprintf(str,  change master to MASTER_LOG_FILE='%s'
        sprintf(str, setup_slave, Maxscale_IP, log_file, log_pos, binlog_port);

        global_result += execute_query(nodes[i], str);
    }
    close_connections();
    return(global_result);
}

int Mariadb_nodes::block_node(int node)
{
    char sys1[1024];
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j REJECT\"", sshkey[node], IP[node], port[node]);
    printf("%s\n", sys1); fflush(stdout);
    return(system(sys1));
}

int Mariadb_nodes::unblock_node(int node)
{
    char sys1[1024];
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j ACCEPT\"", sshkey[node], IP[node], port[node]);
    printf("%s\n", sys1); fflush(stdout);
    return(system(sys1));
}

int Mariadb_nodes::check_nodes()
{
    int res = 0;
    char str[1024];
    printf("Checking nodes\n"); fflush(stdout);
    for (int i = 0; i < N; i++) {
        sprintf(str, "ssh  -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s ls > /dev/null", sshkey[i], IP[i]);
        if (system(str) != 0) {
            printf("Node %d is not available\n", i); fflush(stdout);
            res = 1;
        } else {
            printf("Node %d is OK\n", i); fflush(stdout);
        }
    }
    return(res);
}

int Mariadb_nodes::check_replication(int master)
{
    int res1 = 0;
    char str[1024];
    MYSQL *conn;
    MYSQL_RES *res;
    printf("Checking Master/Slave setup\n"); fflush(stdout);
    for (int i = 0; i < N; i++) {
        conn = open_conn(port[i], IP[i], user_name, password);
        if (mysql_errno(conn) != 0) {
            printf("Error connectiong node %d\n", i); fflush(stdout);
            res1 = 1;
        } else {
            if ( i == master ) {
                // checking master
                if (conn != NULL ) {
                    if(mysql_query(conn, (char *) "SHOW SLAVE HOSTS;") != 0) {
                        printf("%s\n", mysql_error(conn));
                        res1 = 1;
                    } else {
                        res = mysql_store_result(conn);
                        if(res == NULL) {
                            printf("Error: can't get the result description\n"); fflush(stdout);
                            res1 = 1;
                        } else {
                            if (mysql_num_rows(res) != N-1) {
                                printf("Number if slaves is not equal to N-1\n"); fflush(stdout);
                                res1 = 1;
                            }
                        }
                        mysql_free_result(res);
                        do {
                            res = mysql_store_result(conn);
                            mysql_free_result(res);
                        } while ( mysql_next_result(conn) == 0 );
                    }
                }

            } else {
                // checking slave
                if (find_status_field(conn, (char *) "SHOW SLAVE STATUS;", (char *) "Slave_IO_Running", str) != 0) {
                    printf("Slave_IO_Running is not found in SHOW SLAVE STATUS results\n"); fflush(stdout);
                    res1 = 1;
                } else {
                    if (strcmp(str, "Yes") !=0 ) {
                        printf("Slave_IO_Running is not Yes\n"); fflush(stdout);
                        res1 = 1;
                    }
                }
            }
        }
        mysql_close(conn);
    }
    return(res1);
}

int Mariadb_nodes::check_galera()
{
    int res1 = 0;
    char str[1024];
    int cluster_size;
    MYSQL *conn;
    printf("Checking Galera\n"); fflush(stdout);
    for (int i = 0; i < N; i++) {
        conn = open_conn(port[i], IP[i], user_name, password);
        if (mysql_errno(conn) != 0) {
            printf("Error connectiong node %d\n", i);
            res1 = 1;
        } else {
            if (find_status_field(conn, (char *) "SHOW STATUS WHERE Variable_name='wsrep_cluster_size';", (char *) "Value", str) != 0) {
                printf("wsrep_cluster_size is not found in SHOW STATUS LIKE 'wsrep%%' results\n"); fflush(stdout);
                res1 = 1;
            } else {
                sscanf(str, "%d",  &cluster_size);
                if (cluster_size != N ) {
                    printf("wsrep_cluster_size is not %d\n", N); fflush(stdout);
                    res1 = 1;
                }
            }
        }
        mysql_close(conn);
    }
    return(res1);
}

int Mariadb_nodes::wait_all_vm()
{
    int i = 0;

    while ((check_nodes() != 0) && (i < 20)) {
        sleep(10);
    }
    return(check_nodes());
}

int Mariadb_nodes::kill_all_vm()
{
    int res = 0;
    char sys[1024];
    for (int i = 0; i < N; i++) {
        sprintf(sys, "%s %s", kill_vm_command, IP[i]);
        if (system(sys) != 0) {res = 1;}
    }
    return(res);
}

int Mariadb_nodes::start_all_vm()
{
    int res = 0;
    char sys[1024];
    for (int i = 0; i < N; i++) {
        printf("starting node %d\n", i);
        sprintf(sys, "%s %s", start_vm_command, IP[i]);
        if (system(sys) != 0) {res = 1;}
    }
    return(res);
}

int Mariadb_nodes::restart_all_vm()
{
    //kill_all_vm();
    start_all_vm();
    return(wait_all_vm());
}

