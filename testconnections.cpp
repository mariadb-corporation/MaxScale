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
    env = getenv("KillVMCommand"); if (env != NULL) {sprintf(KillVMCommand, "%s", env);}
    env = getenv("GetLogsCommand"); if (env != NULL) {sprintf(GetLogsCommand, "%s", env);}
}

int TestConnections::PrintIP()
{
    int  i;
    printf("Maxscale IP\t%s\n", Maxscale_IP);
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
    Test->PrintIP();

    printf("Trying to connect to MaxScale\n");
    global_result = Test->ConnectMaxscale();
    if (global_result != 0) {
        printf("Error opening connections to MaxScale\n");
    }

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

    Test->CloseMaxscaleConn();

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
