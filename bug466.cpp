
#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    Test->ConnectMaxscale();


    execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- max_slave_replication_lag=120");




    Test->CloseMaxscaleConn();


    global_result += CheckMaxscaleAlive();

    return(global_result);
}


