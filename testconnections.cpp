#include "testconnections.h"


TestConnections::TestConnections()
{
    galera = new Mariadb_nodes((char *)"galera");
    repl   = new Mariadb_nodes((char *)"repl");


    rwsplit_port = 4006;
    readconn_master_port = 4008;
    readconn_slave_port = 4009;
}

int TestConnections::ReadEnv()
{

    char * env;
    int i;
    printf("Reading test setup configuration from environmental variables\n");
    galera->ReadEnv();
    repl->ReadEnv();

    env = getenv("Maxscale_IP"); if (env != NULL) {sprintf(Maxscale_IP, "%s", env);}
    env = getenv("Maxscale_User"); if (env != NULL) {sprintf(Maxscale_User, "%s", env); } else {sprintf(Maxscale_User, "skysql");}
    env = getenv("Maxscale_Password"); if (env != NULL) {sprintf(Maxscale_Password, "%s", env); } else {sprintf(Maxscale_Password, "skysql");}
    env = getenv("Maxscale_sshkey"); if (env != NULL) {sprintf(Maxscale_sshkey, "%s", env); } else {sprintf(Maxscale_sshkey, "skysql");}

    env = getenv("KillVMCommand"); if (env != NULL) {sprintf(KillVMCommand, "%s", env);}
    env = getenv("GetLogsCommand"); if (env != NULL) {sprintf(GetLogsCommand, "%s", env);}
    env = getenv("StartVMCommand"); if (env != NULL) {sprintf(StartVMCommand, "%s", env);}
    env = getenv("SysbenchDir"); if (env != NULL) {sprintf(SysbenchDir, "%s", env);}

    env = getenv("maxdir"); if (env != NULL) {sprintf(maxdir, "%s", env);}
    env = getenv("test_dir"); if (env != NULL) {sprintf(test_dir, "%s", env);}
}

int TestConnections::PrintIP()
{
    int  i;
    printf("Maxscale IP\t%s\n", Maxscale_IP);
    printf("Maxscale User name\t%s\n", Maxscale_User);
    printf("Maxscale Password\t%s\n", Maxscale_Password);
    printf("Maxscale SSH key\t%s\n", Maxscale_sshkey);
    repl->PrintIP();
    galera->PrintIP();
}

int TestConnections::ConnectMaxscale()
{
    return(
        ConnectRWSplit() +
        ConnectReadMaster() +
        ConnectReadSlave()
    );
}

int TestConnections::CloseMaxscaleConn()
{
    mysql_close(conn_master);
    mysql_close(conn_slave);
    mysql_close(conn_rwsplit);
}

int CheckLogErr(char * err_msg, bool expected)
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;
    char * err_log_content;

    Test->ReadEnv();

    printf("Getting logs\n");
    char sys1[4096];
    sprintf(&sys1[0], "%s %s", Test->GetLogsCommand, Test->Maxscale_IP);
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

int FindConnectedSlave(TestConnections* Test, int * global_result)
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    Test->repl->Connect();
    for (int i = 0; i < Test->repl->N; i++) {
        conn_num = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("connections to %d: %u\n", i, conn_num);
        if ((i == 0) && (conn_num != 1)) {printf("There is no connection to master\n"); *global_result = 1;}
        all_conn += conn_num;
        if ((i != 0) && (conn_num != 0)) {current_slave = i;}
    }
    if (all_conn != 2) {printf("total number of connections is not 2, it is %d\n", all_conn); *global_result = 1;}
    printf("Now connected slave node is %d (%s)\n", current_slave, Test->repl->IP[current_slave]);
    Test->repl->CloseConn();
    return(current_slave);
}

int FindConnectedSlave1(TestConnections* Test)
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    Test->repl->Connect();
    for (int i = 0; i < Test->repl->N; i++) {
        conn_num = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("connections to %d: %u\n", i, conn_num); fflush(stdout);
        all_conn += conn_num;
        if ((i != 0) && (conn_num != 0)) {current_slave = i;}
    }
    printf("Now connected slave node is %d (%s)\n", current_slave, Test->repl->IP[current_slave]); fflush(stdout);
    Test->repl->CloseConn();
    return(current_slave);
}


int CheckMaxscaleAlive()
{
    int global_result;
    TestConnections * Test = new TestConnections();
    global_result = 0;

    Test->ReadEnv();
    printf("Connecting to Maxscale\n");
    global_result += Test->ConnectMaxscale();
    printf("Trying simple query against all sevices\n");
    global_result += execute_query(Test->conn_rwsplit, (char *) "show databases;");
    global_result += execute_query(Test->conn_master, (char *) "show databases;");
    global_result += execute_query(Test->conn_slave, (char *) "show databases;");
    Test->CloseMaxscaleConn();
    return(global_result);
}
