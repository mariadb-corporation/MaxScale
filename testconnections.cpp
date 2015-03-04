#include "testconnections.h"
#include <getopt.h>


TestConnections::TestConnections(int argc, char *argv[])
{
    galera = new Mariadb_nodes((char *)"galera");
    repl   = new Mariadb_nodes((char *)"repl");

    test_name = basename(argv[0]);

    rwsplit_port = 4006;
    readconn_master_port = 4008;
    readconn_slave_port = 4009;

    read_env();

    no_maxscale_stop = false;
    no_maxscale_start = false;
    no_nodes_check = false;

    int c;
    bool run_flag = true;

    while (run_flag)
    {
        static struct option long_options[] =
        {

            {"verbose", no_argument, 0, 'v'},
            {"silent", no_argument, 0, 'n'},
            {"help",   no_argument,  0, 'h'},
            {"no-maxscale-start", no_argument, 0, 's'},
            {"no-maxscale-stop",  no_argument, 0, 'd'},
            {"no-nodes-check",  no_argument, 0, 'r'},

            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, "h:",
                         long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c)
        {
        case 'v':
            verbose = true;
            break;

        case 'n':
            verbose = false;
            break;

        case 'h':
            printf ("Options: --help --verbose --silent --no-maxscale-start --no-maxscale-stop");
            break;

        case 's':
            printf ("Maxscale won't be started and Maxscale.cnf won't be uploaded\n");
            no_maxscale_start = true;
            break;

        case 'd':
            printf ("Maxscale won't be stopped\n");
            no_maxscale_stop = true;
            break;

        case 'r':
            printf ("Nodes are not checked before test and are not restarted\n");
            no_nodes_check = true;
            break;

        default:
            run_flag = false;
        }
    }
    if (!no_nodes_check) {
        //  checking repl nodes VMs for availability
        if ((repl->check_nodes() != 0) || (repl->check_replication(0) != 0)) {
            printf("Backend broken! Restarting replication nodes\n");
            repl->restart_all_vm();
            repl->start_replication();
        }
        //  checking galera nodes VMs for availability
        if ((galera->check_nodes() != 0) || (galera->check_galera() != 0)) {
            printf("Backend broken! Restarting Galera nodes\n");
            galera->restart_all_vm();
            galera->start_galera();
        }
    }
    if (!no_maxscale_start) {init_maxscale();}
}

TestConnections::TestConnections()
{
    galera = new Mariadb_nodes((char *)"galera");
    repl   = new Mariadb_nodes((char *)"repl");

    rwsplit_port = 4006;
    readconn_master_port = 4008;
    readconn_slave_port = 4009;

    read_env();
}


int TestConnections::read_env()
{

    char * env;
    int i;
    printf("Reading test setup configuration from environmental variables\n");
    galera->read_env();
    repl->read_env();

    env = getenv("maxscale_IP"); if (env != NULL) {sprintf(maxscale_IP, "%s", env);}
    env = getenv("maxscale_user"); if (env != NULL) {sprintf(maxscale_user, "%s", env); } else {sprintf(maxscale_user, "skysql");}
    env = getenv("maxscale_uassword"); if (env != NULL) {sprintf(maxscale_password, "%s", env); } else {sprintf(maxscale_password, "skysql");}
    env = getenv("maxscale_sshkey"); if (env != NULL) {sprintf(maxscale_sshkey, "%s", env); } else {sprintf(maxscale_sshkey, "skysql");}

    env = getenv("kill_vm_command"); if (env != NULL) {sprintf(kill_vm_command, "%s", env);}
    env = getenv("get_logs_command"); if (env != NULL) {sprintf(get_logs_command, "%s", env);}
    env = getenv("start_vm_command"); if (env != NULL) {sprintf(start_vm_command, "%s", env);}
    env = getenv("sysbench_dir"); if (env != NULL) {sprintf(sysbench_dir, "%s", env);}

    env = getenv("maxdir"); if (env != NULL) {sprintf(maxdir, "%s", env);}
    env = getenv("test_dir"); if (env != NULL) {sprintf(test_dir, "%s", env);}
}

int TestConnections::print_env()
{
    int  i;
    printf("Maxscale IP\t%s\n", maxscale_IP);
    printf("Maxscale User name\t%s\n", maxscale_user);
    printf("Maxscale Password\t%s\n", maxscale_password);
    printf("Maxscale SSH key\t%s\n", maxscale_sshkey);
    repl->print_env();
    galera->print_env();
}

int TestConnections::init_maxscale()
{
    char str[4096];
    pid_t pid = fork();
    if (!pid) {
        sprintf(str, "export test_name=%s; %s/configure_maxscale.sh", test_name, test_dir);
        printf("Executing configure_maxscale.sh\n"); fflush(stdout);
        if (system(str) !=0) {
            printf("configure_maxscale.sh executing FAILED!\n"); fflush(stdout);
            exit(1);
        } else {
            exit(0);
        }
    }
    else {

        printf("Waiting 15 seconds\n"); fflush(stdout);
        sleep(15);
        kill(pid, SIGKILL);
    }
}

int TestConnections::connect_maxscale()
{
    return(
                connect_rwsplit() +
                connect_readconn_master() +
                connect_readconn_slave()
                );
}

int TestConnections::close_maxscale_connections()
{
    mysql_close(conn_master);
    mysql_close(conn_slave);
    mysql_close(conn_rwsplit);
}

int TestConnections::copy_all_logs()
{
    char str[4096];

    if (!no_maxscale_stop) {
        sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@%s \"service maxscale stop\"", maxscale_sshkey, maxscale_IP);
        system(str);
    }
    sprintf(str, "%s/copy_logs.sh %s", test_dir, test_name);
    printf("Executing copy_logs.sh\n"); fflush(stdout);
    if (system(str) !=0) {
        printf("copy_logs.sh executing FAILED!\n"); fflush(stdout);
        return(1);
    } else {
        printf("copy_logs.sh OK!\n"); fflush(stdout);
        return(0);
    }
}


int check_log_err(char * err_msg, bool expected)
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;
    char * err_log_content;

    Test->read_env();

    printf("Getting logs\n");
    char sys1[4096];
    sprintf(&sys1[0], "%s %s", Test->get_logs_command, Test->maxscale_IP);
    printf("Executing: %s\n", sys1);
    fflush(stdout);
    system(sys1);

    printf("Reading err_log\n");
    global_result += ReadLog((char *) "skygw_err1.log", &err_log_content);

    if (expected) {
        if (strstr(err_log_content, err_msg) == NULL) {
            global_result++;
            printf("There is NO \"%s\" error in the log\n", err_msg);
        } else {
            printf("There is proper \"%s \" error in the log\n", err_msg);
        }}
    else {
        if (strstr(err_log_content, err_msg) != NULL) {
            global_result++;
            printf("There is UNEXPECTED error \"%s\" error in the log\n", err_msg);
        } else {
            printf("There are no unxpected errors \"%s \" error in the log\n", err_msg);
        }
    }


    if (err_log_content != NULL) {free(err_log_content);}

    return global_result;
}

int find_connected_slave(TestConnections* Test, int * global_result)
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    Test->repl->connect();
    for (int i = 0; i < Test->repl->N; i++) {
        conn_num = get_conn_num(Test->repl->nodes[i], Test->maxscale_IP, (char *) "test");
        printf("connections to %d: %u\n", i, conn_num);
        if ((i == 0) && (conn_num != 1)) {printf("There is no connection to master\n"); *global_result = 1;}
        all_conn += conn_num;
        if ((i != 0) && (conn_num != 0)) {current_slave = i;}
    }
    if (all_conn != 2) {printf("total number of connections is not 2, it is %d\n", all_conn); *global_result = 1;}
    printf("Now connected slave node is %d (%s)\n", current_slave, Test->repl->IP[current_slave]);
    Test->repl->close_connections();
    return(current_slave);
}

int find_connected_slave1(TestConnections* Test)
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    Test->repl->connect();
    for (int i = 0; i < Test->repl->N; i++) {
        conn_num = get_conn_num(Test->repl->nodes[i], Test->maxscale_IP, (char *) "test");
        printf("connections to %d: %u\n", i, conn_num); fflush(stdout);
        all_conn += conn_num;
        if ((i != 0) && (conn_num != 0)) {current_slave = i;}
    }
    printf("Now connected slave node is %d (%s)\n", current_slave, Test->repl->IP[current_slave]); fflush(stdout);
    Test->repl->close_connections();
    return(current_slave);
}


int check_maxscale_alive()
{
    int global_result;
    TestConnections * Test = new TestConnections();
    global_result = 0;

    //Test->ReadEnv();
    printf("Connecting to Maxscale\n");
    global_result += Test->connect_maxscale();
    printf("Trying simple query against all sevices\n");
    printf("RWSplit ");
    if (execute_query(Test->conn_rwsplit, (char *) "show databases;") == 0) {
        printf("OK\n"); fflush(stdout);
    } else {
        printf("FAILED\n"); fflush(stdout);
        global_result++;
    }
    printf("ReadConn Master ");
    if (execute_query(Test->conn_master, (char *) "show databases;") == 0) {
        printf("OK\n"); fflush(stdout);
    } else {
        printf("FAILED\n"); fflush(stdout);
        global_result++;
    }
    printf("ReadConn Slave ");
    if (execute_query(Test->conn_slave, (char *) "show databases;") == 0) {
        printf("OK\n"); fflush(stdout);
    } else {
        printf("FAILED\n"); fflush(stdout);
        global_result++;
    }

    Test->close_maxscale_connections();
    return(global_result);
}
