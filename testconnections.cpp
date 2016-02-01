#include "testconnections.h"
#include "sql_t1.h"
#include <getopt.h>
#include <time.h>
#include <libgen.h>

TestConnections::TestConnections(int argc, char *argv[])
{
    galera = new Mariadb_nodes((char *)"galera");
    repl   = new Mariadb_nodes((char *)"repl");

    test_name = basename(argv[0]);

    rwsplit_port = 4006;
    readconn_master_port = 4008;
    readconn_slave_port = 4009;
    binlog_port = 5306;

    global_result = 0;

    read_env();

    char short_path[1024];
    strcpy(short_path, dirname(argv[0]));
    realpath(short_path, test_dir);
    printf("test_dir is %s\n", test_dir);
    sprintf(get_logs_command, "%s/get_logs.sh", test_dir);

    no_maxscale_stop = false;
    no_maxscale_start = false;
    //no_nodes_check = false;

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
        //  checking all nodes and restart if needed
        repl->unblock_all_nodes();
        galera->unblock_all_nodes();
        repl->check_and_restart_nodes_vm();
        galera->check_and_restart_nodes_vm();
        //  checking repl
        if (repl->check_replication(0) != 0) {
            printf("Backend broken! Restarting replication nodes\n");
            repl->start_replication();
        }
        //  checking galera
        if  (galera->check_galera() != 0) {
            printf("Backend broken! Restarting Galera nodes\n");
            galera->start_galera();
        }
    }

    repl->flush_hosts();
    galera->flush_hosts();

    if ((repl->check_replication(0) != 0) || (galera->check_galera() != 0)) {
        printf("****** BACKEND IS STILL BROKEN! Exiting\n *****");
        exit(200);
    }
    //repl->start_replication();
    if (!no_maxscale_start) {init_maxscale();}
    timeout = 99999;
    pthread_create( &timeout_thread_p, NULL, timeout_thread, this);
    gettimeofday(&start_time, NULL);
}

TestConnections::TestConnections()
{
    galera = new Mariadb_nodes((char *)"galera");
    repl   = new Mariadb_nodes((char *)"repl");

    global_result = 0;

    rwsplit_port = 4006;
    readconn_master_port = 4008;
    readconn_slave_port = 4009;

    read_env();

    timeout = 99999;
    pthread_create( &timeout_thread_p, NULL, timeout_thread, this);
    gettimeofday(&start_time, NULL);
}

void TestConnections::add_result(int result, const char *format, ...)
{
    timeval t2;
    gettimeofday(&t2, NULL);
    double elapsedTime = (t2.tv_sec - start_time.tv_sec);
    elapsedTime += (double) (t2.tv_usec - start_time.tv_usec) / 1000000.0;

    if (result != 0) {
        global_result += result;

        printf("%04f: TEST_FAILED! ", elapsedTime);

        va_list argp;
        va_start(argp, format);
        vprintf(format, argp);
        va_end(argp);
    }
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
    env = getenv("maxscale_password"); if (env != NULL) {sprintf(maxscale_password, "%s", env); } else {sprintf(maxscale_password, "skysql");}
    env = getenv("maxadmin_password"); if (env != NULL) {sprintf(maxadmin_password, "%s", env); } else {sprintf(maxadmin_password, "mariadb");}
    env = getenv("maxscale_sshkey"); if (env != NULL) {sprintf(maxscale_sshkey, "%s", env); } else {sprintf(maxscale_sshkey, "skysql");}

    //env = getenv("get_logs_command"); if (env != NULL) {sprintf(get_logs_command, "%s", env);}

    env = getenv("sysbench_dir"); if (env != NULL) {sprintf(sysbench_dir, "%s", env);}

    env = getenv("maxdir"); if (env != NULL) {sprintf(maxdir, "%s", env);}
    env = getenv("maxscale_cnf"); if (env != NULL) {sprintf(maxscale_cnf, "%s", env);} else {sprintf(maxscale_cnf, "/etc/maxscale.cnf");}
    env = getenv("maxscale_log_dir"); if (env != NULL) {sprintf(maxscale_log_dir, "%s", env);} else {sprintf(maxscale_log_dir, "%s/logs/", maxdir);}
    env = getenv("maxscale_binlog_dir"); if (env != NULL) {sprintf(maxscale_binlog_dir, "%s", env);} else {sprintf(maxscale_binlog_dir, "%s/Binlog_Service/", maxdir);}
    //env = getenv("test_dir"); if (env != NULL) {sprintf(test_dir, "%s", env);}
    env = getenv("maxscale_access_user"); if (env != NULL) {sprintf(maxscale_access_user, "%s", env);}
    env = getenv("maxscale_access_sudo"); if (env != NULL) {sprintf(maxscale_access_sudo, "%s", env);}
    ssl = false;
    env = getenv("ssl"); if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) )) {ssl = true;}
    env = getenv("mysql51_only"); if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) )) {no_nodes_check = true;}

    env = getenv("maxscale_hostname"); if (env != NULL) {sprintf(maxscale_hostname, "%s", env);} else {sprintf(maxscale_hostname, "%s", maxscale_IP);}

    if (strcmp(maxscale_access_user, "root") == 0) {
        sprintf(maxscale_access_homedir, "/%s/", maxscale_access_user);
    } else {
        sprintf(maxscale_access_homedir, "/home/%s/", maxscale_access_user);
    }

    env = getenv("smoke"); if ((env != NULL) && ((strcasecmp(env, "yes") == 0) || (strcasecmp(env, "true") == 0) )) {smoke = true;} else {smoke = false;}
}

int TestConnections::print_env()
{
    int  i;
    printf("Maxscale IP\t%s\n", maxscale_IP);
    printf("Maxscale User name\t%s\n", maxscale_user);
    printf("Maxscale Password\t%s\n", maxscale_password);
    printf("Maxscale SSH key\t%s\n", maxscale_sshkey);
    printf("Maxadmin password\t%s\n", maxadmin_password);
    printf("Access user\t%s\n", maxscale_access_user);
    repl->print_env();
    galera->print_env();
}

int TestConnections::init_maxscale()
{
    char str[4096];
    sprintf(str, "export test_name=%s; export test_dir=%s; %s/configure_maxscale.sh", test_name, test_dir, test_dir);
    printf("\nExecuting configure_maxscale.sh\n"); fflush(stdout);
    if (system(str) !=0) {
        printf("configure_maxscale.sh executing FAILED!\n"); fflush(stdout);
        return(1);
    }
    fflush(stdout);
    printf("Waiting 15 seconds\n"); fflush(stdout);
    sleep(15);
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

int TestConnections::restart_maxscale()
{
    int res = ssh_maxscale((char *) "service maxscale restart", true);
    sleep(10);
    return(res);
}

int TestConnections::start_maxscale()
{
    int res = ssh_maxscale((char *) "service maxscale start", true);
    sleep(10);
    return(res);
}

int TestConnections::stop_maxscale()
{
    int res = ssh_maxscale((char *) "service maxscale stop", true);
    return(res);
}

int TestConnections::copy_all_logs()
{
    char str[4096];
    set_timeout(300);
    sprintf(str, "%s/copy_logs.sh %s", test_dir, test_name);
    tprintf("Executing %s\n", str);
    if (system(str) !=0) {
        tprintf("copy_logs.sh executing FAILED!\n");
        return(1);
    } else {
        tprintf("copy_logs.sh OK!\n");
        return(0);
    }
}

int TestConnections::start_binlog()
{
    char sys1[4096];
    MYSQL * binlog;
    char log_file[256];
    char log_pos[256];
    char cmd_opt[256];
    char version_str[1024];
    int i;
    int global_result = 0;
    bool no_pos;

    no_pos = repl->no_set_pos;

    switch (binlog_cmd_option) {
    case 1:
        sprintf(cmd_opt, "--binlog-checksum=CRC32");
        break;
    case 2:
        sprintf(cmd_opt, "--binlog-checksum=NONE");
        break;
    default:
        sprintf(cmd_opt, " ");
    }

    repl->connect();
    find_field(repl->nodes[0], "SELECT @@VERSION", "@@version", version_str);
    /*execute_query(repl->nodes[0], "reset master");*/
    for (i = 0; i < repl->N; i++) {
        execute_query(repl->nodes[i], "stop slave");
        execute_query(repl->nodes[i], "reset slave");
    }
    repl->close_connections();

    tprintf("Master server version %s\n", version_str);

    if (strstr(version_str, "5.5") != NULL) {
        sprintf(&sys1[0], "sed -i \"s/,mariadb10-compatibility=1//\" %s", maxscale_cnf);
        tprintf("%s\n", sys1);
        add_result(ssh_maxscale(sys1, true), "Error editing maxscale.cnf");
    }

    tprintf("Testing binlog when MariaDB is started with '%s' option\n", cmd_opt);

    binlog = open_conn_no_db(binlog_port, maxscale_IP, repl->user_name, repl->password, ssl);
    execute_query(binlog, (char *) "stop slave");
    execute_query(binlog, (char *) "reset slave");
    mysql_close(binlog);

    tprintf("Stopping maxscale\n");
    add_result(stop_maxscale(), "Maxscale stopping failed\n");

    tprintf("Stopping all backend nodes\n");
    add_result(repl->stop_nodes(), "Nodes stopping failed\n");

    tprintf("Removing all binlog data from Maxscale node\n");
    sprintf(&sys1[0], "rm -rf %s", maxscale_binlog_dir);
    tprintf("%s\n", sys1);
    add_result(ssh_maxscale(sys1, true), "Removing binlog data failed\n");

    tprintf("Creating binlog dir\n");
    sprintf(&sys1[0], "mkdir -p %s", maxscale_binlog_dir);
    tprintf("%s\n", sys1);
    add_result(ssh_maxscale(sys1, true), "Creating binlog data dir failed\n");

    tprintf("ls binlog data dir on Maxscale node\n");
    sprintf(&sys1[0], "ls -la %s/", maxscale_binlog_dir);
    tprintf("%s\n", sys1);
    add_result(ssh_maxscale(sys1, true), "ls failed\n");

    tprintf("Set 'maxscale' as a owner of binlog dir\n");
    sprintf(&sys1[0], "%s mkdir -p %s; %s chown maxscale:maxscale -R %s", maxscale_access_sudo, maxscale_binlog_dir, maxscale_access_sudo, maxscale_binlog_dir);
    tprintf("%s\n", sys1);
    add_result(ssh_maxscale(sys1, false), "directory ownership change failed\n");

    tprintf("Starting back Master\n");
    add_result(repl->start_node(0, cmd_opt), "Master start failed\n");

    MYSQL * master = open_conn_no_db(repl->port[0], repl->IP[0], repl->user_name, repl->password, ssl);
    execute_query(master, (char*) "reset master");
    mysql_close(master);

    for (i = 1; i < repl->N; i++) {
        tprintf("Starting node %d\n", i);
        add_result(repl->start_node(i, cmd_opt), "Node %d start failed\n", i+1);
    }
    sleep(5);

    tprintf("Connecting to all backend nodes\n");
    add_result(repl->connect(), "Connecting to backed failed\n");
    tprintf("Dropping t1 table on all backend nodes\n");
    for (i = 0; i < repl->N; i++)
    {
        execute_query(repl->nodes[i], (char *) "DROP TABLE IF EXISTS t1;");
    }
    tprintf("'reset master' query to node 0\n");fflush(stdout);
    execute_query(repl->nodes[0], (char *) "reset master;");

    tprintf("show master status\n");
    find_field(repl->nodes[0], (char *) "show master status", (char *) "File", &log_file[0]);
    find_field(repl->nodes[0], (char *) "show master status", (char *) "Position", &log_pos[0]);
    tprintf("Real master file: %s\n", log_file);
    tprintf("Real master pos : %s\n", log_pos);

    tprintf("Stopping first slave (node 1)\n");
    try_query(repl->nodes[1], (char *) "stop slave;");
    //repl->no_set_pos = true;
    repl->no_set_pos = false;
    tprintf("Configure first backend slave node to be slave of real master\n");
    repl->set_slave(repl->nodes[1], repl->IP[0],  repl->port[0], log_file, log_pos);

    tprintf("Starting back Maxscale\n");
    add_result(start_maxscale(), "Maxscale start failed\n");

    tprintf("Connecting to MaxScale binlog router (with any DB)\n");
    binlog = open_conn_no_db(binlog_port, maxscale_IP, repl->user_name, repl->password, ssl);

    add_result(mysql_errno(binlog), "Error connection to binlog router %s\n", mysql_error(binlog));

    repl->no_set_pos = true;
    tprintf("configuring Maxscale binlog router\n");
    repl->set_slave(binlog, repl->IP[0], repl->port[0], log_file, log_pos);
    try_query(binlog, "start slave");

    repl->no_set_pos = false;

    // get Master status from Maxscale binlog
    tprintf("show master status\n");fflush(stdout);
    find_field(binlog, (char *) "show master status", (char *) "File", &log_file[0]);
    find_field(binlog, (char *) "show master status", (char *) "Position", &log_pos[0]);

    tprintf("Maxscale binlog master file: %s\n", log_file); fflush(stdout);
    tprintf("Maxscale binlog master pos : %s\n", log_pos); fflush(stdout);

    tprintf("Setup all backend nodes except first one to be slaves of binlog Maxscale node\n");fflush(stdout);
    for (i = 2; i < repl->N; i++) {
        try_query(repl->nodes[i], (char *) "stop slave;");
        repl->set_slave(repl->nodes[i],  maxscale_IP, binlog_port, log_file, log_pos);
    }
    repl->close_connections();
    mysql_close(binlog);
    repl->no_set_pos = no_pos;
    return(global_result);
}

int TestConnections::start_mm()
{
    int i;
    char log_file1[256];
    char log_pos1[256];
    char log_file2[256];
    char log_pos2[256];
    char sys1[1024];

    tprintf("Stopping maxscale\n");fflush(stdout);
    int global_result = stop_maxscale();

    tprintf("Stopping all backend nodes\n");fflush(stdout);
    global_result += repl->stop_nodes();

    for (i = 0; i < 2; i++) {
        tprintf("Starting back node %d\n", i);
        global_result += repl->start_node(i, (char *) "");
    }

    repl->connect();
    for (i = 0; i < 2; i++) {
        execute_query(repl->nodes[i], (char *) "stop slave");
        execute_query(repl->nodes[i], (char *) "reset master");
    }

    execute_query(repl->nodes[0], (char *) "SET GLOBAL READ_ONLY=ON");

    find_field(repl->nodes[0], (char *) "show master status", (char *) "File", log_file1);
    find_field(repl->nodes[0], (char *) "show master status", (char *) "Position", log_pos1);

    find_field(repl->nodes[1], (char *) "show master status", (char *) "File", log_file2);
    find_field(repl->nodes[1], (char *) "show master status", (char *) "Position", log_pos2);

    repl->set_slave(repl->nodes[0], repl->IP[1],  repl->port[1], log_file2, log_pos2);
    repl->set_slave(repl->nodes[1], repl->IP[0],  repl->port[0], log_file1, log_pos1);

    repl->close_connections();

    tprintf("Starting back Maxscale\n");  fflush(stdout);
    global_result += start_maxscale();

    return(global_result);
}

void TestConnections::check_log_err(char * err_msg, bool expected)
{

    char * err_log_content;

    tprintf("Getting logs\n");
    char sys1[4096];
    set_timeout(100);
    sprintf(&sys1[0], "rm *.log; %s %s", get_logs_command, maxscale_IP);
    //tprintf("Executing: %s\n", sys1);
    system(sys1);
    set_timeout(50);

    tprintf("Reading maxscale1.log\n");
    if ( read_log((char *) "maxscale1.log", &err_log_content) != 0) {
        //tprintf("Reading maxscale1.log\n");
        //read_log((char *) "skygw_err1.log", &err_log_content);
        add_result(1, "Error reading log\n");
    } else {

        if (expected) {
            if (strstr(err_log_content, err_msg) == NULL) {
                add_result(1, "There is NO \"%s\" error in the log\n", err_msg);
            } else {
                tprintf("There is proper \"%s \" error in the log\n", err_msg);
            }}
        else {
            if (strstr(err_log_content, err_msg) != NULL) {
                add_result(1, "There is UNEXPECTED error \"%s\" error in the log\n", err_msg);
            } else {
                tprintf("There are no unxpected errors \"%s \" error in the log\n", err_msg);
            }
        }

    }
    if (err_log_content != NULL) {free(err_log_content);}
}

int TestConnections::find_connected_slave(int * global_result)
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    repl->connect();
    for (int i = 0; i < repl->N; i++) {
        conn_num = get_conn_num(repl->nodes[i], maxscale_IP, maxscale_hostname, (char *) "test");
        tprintf("connections to %d: %u\n", i, conn_num);
        if ((i == 0) && (conn_num != 1)) {tprintf("There is no connection to master\n"); *global_result = 1;}
        all_conn += conn_num;
        if ((i != 0) && (conn_num != 0)) {current_slave = i;}
    }
    if (all_conn != 2) {tprintf("total number of connections is not 2, it is %d\n", all_conn); *global_result = 1;}
    tprintf("Now connected slave node is %d (%s)\n", current_slave, repl->IP[current_slave]);
    repl->close_connections();
    return(current_slave);
}

int TestConnections::find_connected_slave1()
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    repl->connect();
    for (int i = 0; i < repl->N; i++) {
        conn_num = get_conn_num(repl->nodes[i], maxscale_IP, maxscale_hostname, (char *) "test");
        tprintf("connections to %d: %u\n", i, conn_num);
        all_conn += conn_num;
        if ((i != 0) && (conn_num != 0)) {current_slave = i;}
    }
    tprintf("Now connected slave node is %d (%s)\n", current_slave, repl->IP[current_slave]);
    repl->close_connections();
    return(current_slave);
}

int TestConnections::check_maxscale_alive()
{
    int gr = global_result;
    set_timeout(10);
    tprintf("Connecting to Maxscale\n");
    add_result(connect_maxscale(), "Can not connect to Maxscale\n");
    tprintf("Trying simple query against all sevices\n");
    tprintf("RWSplit \n");
    set_timeout(10);
    try_query(conn_rwsplit, (char *) "show databases;");
    tprintf("ReadConn Master \n");
    set_timeout(10);
    try_query(conn_master, (char *) "show databases;");
    tprintf("ReadConn Slave \n");
    set_timeout(10);
    try_query(conn_slave, (char *) "show databases;");
    set_timeout(10);
    close_maxscale_connections()    ;
    add_result(global_result-gr, "Maxscale is not alive\n");
    stop_timeout();
    return(global_result-gr);
}

bool TestConnections::test_maxscale_connections(bool rw_split, bool rc_master, bool rc_slave)
{
    bool rval = true;
    int rc;

    tprintf("Testing RWSplit, expecting %s\n", (rw_split ? "success" : "failure"));
    rc = execute_query(conn_rwsplit, "select 1");
    if((rc == 0) != rw_split)
    {
        tprintf("Error: Query %s\n", (rw_split ? "failed" : "succeeded"));
        rval = false;
    }

    tprintf("Testing ReadConnRoute Master, expecting %s\n", (rc_master ? "success" : "failure"));
    rc = execute_query(conn_master, "select 1");
    if((rc == 0) != rc_master)
    {
        tprintf("Error: Query %s", (rc_master ? "failed" : "succeeded"));
        rval = false;
    }

    tprintf("Testing ReadConnRoute Slave, expecting %s", (rc_slave ? "success" : "failure"));
    rc = execute_query(conn_slave, "select 1");
    if((rc == 0) != rc_slave)
    {
        tprintf("Error: Query %s", (rc_slave ? "failed" : "succeeded"));
        rval = false;
    }
    return rval;
}

void TestConnections::generate_ssh_cmd(char * cmd, char * ssh, bool sudo)
{
    if (sudo)
    {
        sprintf(cmd, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet %s@%s '%s %s'",
                maxscale_sshkey, maxscale_access_user, maxscale_IP, maxscale_access_sudo, ssh);
    } else
    {
        sprintf(cmd, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet %s@%s '%s\'",
                maxscale_sshkey, maxscale_access_user, maxscale_IP, ssh);
    }
}


char* TestConnections::ssh_maxscale_output(char* ssh, bool sudo)
{
    char sys[strlen(ssh) + 1024];

    generate_ssh_cmd(sys, ssh, sudo);

    FILE *output = popen(sys, "r");
    char buffer[1024];
    size_t rsize = sizeof(buffer);
    char* result = (char*)calloc(rsize, sizeof(char));

    while(fgets(buffer, sizeof(buffer), output))
    {
        result = (char*)realloc(result, sizeof(buffer) + rsize);
        strcat(result, buffer);
    }

    return result;
}

int  TestConnections::ssh_maxscale(char* ssh, bool sudo)
{
    char sys[strlen(ssh) + 1024];

    generate_ssh_cmd(sys, ssh, sudo);

    return system(sys);
}


int TestConnections::reconfigure_maxscale(char* config_template)
{
    char cmd[1024];
    setenv("test_name",config_template,1);
    sprintf(cmd,"%s/configure_maxscale.sh",test_dir);
    return system(cmd); 
}

int TestConnections::create_connections(int conn_N)
{
    int i;
    int local_result = 0;
    MYSQL * rwsplit_conn[conn_N];
    MYSQL * master_conn[conn_N];
    MYSQL * slave_conn[conn_N];
    MYSQL * galera_conn[conn_N];


    tprintf("Opening %d connections to each router\n", conn_N);
    for (i = 0; i < conn_N; i++) {
        set_timeout(20);
        tprintf("opening %d-connection: ", i+1);

        printf("RWSplit \t");
        rwsplit_conn[i] = open_rwsplit_connection();
        if (!rwsplit_conn[i]) { local_result++; tprintf("RWSplit connection failed\n");}

        printf("ReadConn master \t");
        master_conn[i] = open_readconn_master_connection();
        if ( mysql_errno(master_conn[i]) != 0 ) { local_result++; tprintf("ReadConn master connection failed, error: %s\n", mysql_error(master_conn[i]) );}
        printf("ReadConn slave \t");
        slave_conn[i] = open_readconn_slave_connection();
        if ( mysql_errno(slave_conn[i]) != 0 )  { local_result++; tprintf("ReadConn slave connection failed, error: %s\n", mysql_error(slave_conn[i]) );}
        printf("galera \n");
        galera_conn[i] = open_conn(4016, maxscale_IP, maxscale_user, maxscale_password, ssl);
        if ( mysql_errno(galera_conn[i]) != 0)  { local_result++; tprintf("Galera connection failed, error: %s\n", mysql_error(galera_conn[i]));}
    }
    for (i = 0; i < conn_N; i++) {
        set_timeout(10);
        tprintf("Trying query against %d-connection: ", i+1);
        tprintf("RWSplit \t");
        local_result += execute_query(rwsplit_conn[i], "select 1;");
        tprintf("ReadConn master \t");
        local_result += execute_query(master_conn[i], "select 1;");
        tprintf("ReadConn slave \t");
        local_result += execute_query(slave_conn[i], "select 1;");
        tprintf("galera \n");
        local_result += execute_query(galera_conn[i], "select 1;");
    }

    //global_result += check_pers_conn(Test, pers_conn_expected);
    tprintf("Closing all connections\n");
    for (i=0; i<conn_N; i++) {
        set_timeout(10);
        mysql_close(rwsplit_conn[i]);
        mysql_close(master_conn[i]);
        mysql_close(slave_conn[i]);
        mysql_close(galera_conn[i]);
    }
    stop_timeout();
    //sleep(5);
    return(local_result);
}

int TestConnections::get_client_ip(char * ip)
{
    MYSQL * conn;
    MYSQL_RES *res;
    MYSQL_ROW row;
    int ret = 1;
    unsigned long long int num_fields;
    //unsigned long long int row_i=0;
    unsigned long long int rows;
    unsigned long long int i;
    unsigned int conn_num = 0;

    connect_rwsplit();
    if (execute_query(conn_rwsplit, (char *) "CREATE DATABASE IF NOT EXISTS db_to_check_clent_ip") !=0 ) {
        return(ret);
    }
    close_rwsplit();
    conn = open_conn_db(rwsplit_port, maxscale_IP, (char *) "db_to_check_clent_ip", maxscale_user, maxscale_password, ssl);

    if (conn != NULL) {
        if(mysql_query(conn, "show processlist;") != 0) {
            printf("Error: can't execute SQL-query: show processlist\n");
            printf("%s\n\n", mysql_error(conn));
            conn_num = 0;
        } else {
            res = mysql_store_result(conn);
            if(res == NULL) {
                printf("Error: can't get the result description\n");
                conn_num = -1;
            } else {
                num_fields = mysql_num_fields(res);
                rows = mysql_num_rows(res);
                for (i = 0; i < rows; i++) {
                    row = mysql_fetch_row(res);
                    if ( (row[2] != NULL ) && (row[3] != NULL) ) {
                        if  (strstr(row[3], "db_to_check_clent_ip") != NULL) {
                            ret = 0;
                            strcpy(ip, row[2]);
                        }
                    }
                }
            }
            mysql_free_result(res);
        }
    }

    mysql_close(conn);
    return(ret);
}

int TestConnections::set_timeout(int timeout_seconds)
{
    timeout = timeout_seconds;
    return(0);
}

int TestConnections::stop_timeout()
{
    timeout = 99999;
    return(0);
}

int TestConnections::tprintf(const char *format, ...)
{
    timeval t2;
    gettimeofday(&t2, NULL);
    double elapsedTime = (t2.tv_sec - start_time.tv_sec);
    elapsedTime += (double) (t2.tv_usec - start_time.tv_usec) / 1000000.0;

    printf("%04f: ", elapsedTime);

    va_list argp;
    va_start(argp, format);
    vprintf(format, argp);
    va_end(argp);
}

void *timeout_thread( void *ptr )
{
    TestConnections * Test = (TestConnections *) ptr;
    struct timespec tim;
    while (Test->timeout > 0) {
        tim.tv_sec = 1;
        tim.tv_nsec = 0;
        nanosleep(&tim, NULL);
        Test->timeout--;
    }
    Test->tprintf("\n **** Timeout! *** \n");
    Test->copy_all_logs();
    exit(250);
}

int TestConnections::insert_select(int N)
{
    int global_result = 0;
    tprintf("Create t1\n");
    set_timeout(30);
    create_t1(conn_rwsplit);
    tprintf("Insert data into t1\n");
    set_timeout(30);
    insert_into_t1(conn_rwsplit, N);

    tprintf("SELECT: rwsplitter\n");
    set_timeout(30);
    global_result += select_from_t1(conn_rwsplit, N);
    tprintf("SELECT: master\n");
    set_timeout(30);
    global_result += select_from_t1(conn_master, N);
    tprintf("SELECT: slave\n");
    set_timeout(30);
    global_result += select_from_t1(conn_slave, N);
    tprintf("Sleeping to let replication happen\n");
    stop_timeout();
    if (smoke) {
        sleep(30);
    } else {
        sleep(180);
    }
    for (int i=0; i<repl->N; i++) {
        tprintf("SELECT: directly from node %d\n", i);
        set_timeout(30);
        global_result += select_from_t1(repl->nodes[i], N);
    }
    return(global_result);
}

int TestConnections::use_db(char * db)
{
    int local_result = 0;
    char sql[100];

    sprintf(sql, "USE %s;", db);
    set_timeout(20);
    tprintf("selecting DB '%s' for rwsplit\n", db);
    local_result += execute_query(conn_rwsplit, sql);
    tprintf("selecting DB '%s' for readconn master\n", db);
    local_result += execute_query(conn_slave, sql);
    tprintf("selecting DB '%s' for readconn slave\n", db);
    local_result += execute_query(conn_master, sql);
    for (int i = 0; i < repl->N; i++) {
        tprintf("selecting DB '%s' for direct connection to node %d\n", db, i);
        local_result += execute_query(repl->nodes[i], sql);
    }
    return(local_result);
}

int TestConnections::check_t1_table(bool presence, char * db)
{
    char * expected;
    char * actual;
    set_timeout(30);
    int gr = global_result;
    if (presence) {
        expected = (char *) "";
        actual   = (char *) "NOT";
    } else {
        expected = (char *) "NOT";
        actual   = (char *) "";
    }

    add_result(use_db(db), "use db failed\n");

    tprintf("Checking: table 't1' should %s be found in '%s' database\n", expected, db);
    if ( ((check_if_t1_exists(conn_rwsplit) >  0) && (!presence) ) ||
         ((check_if_t1_exists(conn_rwsplit) == 0) && (presence) ))
    {add_result(1, "Table t1 is %s found in '%s' database using RWSplit\n", actual, db); } else {
        tprintf("RWSplit: ok\n");
    }
    if ( ((check_if_t1_exists(conn_master) >  0) && (!presence) ) ||
         ((check_if_t1_exists(conn_master) == 0) && (presence) ))
    {add_result(1, "Table t1 is %s found in '%s' database using Readconnrouter with router option master\n", actual, db); } else {
        tprintf("ReadConn master: ok\n");
    }
    if ( ((check_if_t1_exists(conn_slave) >  0) && (!presence) ) ||
         ((check_if_t1_exists(conn_slave) == 0) && (presence) ))
    {add_result(1, "Table t1 is %s found in '%s' database using Readconnrouter with router option slave\n", actual, db); } else {
        tprintf("ReadConn slave: ok\n");
    }
    tprintf("Sleeping to let replication happen\n");
    stop_timeout();
    sleep(60);
    for (int i=0; i< repl->N; i++) {
        set_timeout(30);
        if ( ((check_if_t1_exists(repl->nodes[i]) >  0) && (!presence) ) ||
             ((check_if_t1_exists(repl->nodes[i]) == 0) && (presence) ))
        {add_result(1, "Table t1 is %s found in '%s' database using direct connect to node %d\n", actual, db, i); } else {
            tprintf("Node %d: ok\n", i);
        }
    }
    return(global_result-gr);
}

int TestConnections::try_query(MYSQL *conn, const char *sql)
{
    int res = execute_query(conn, sql);
    add_result(res, "Query '%s' failed!\n", sql);
    return(res);
}
