

#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    Test->ConnectMaxscale();


    execute_query(Test->maxscales->conn_rwsplit[0], (char *) "select @@server_id; -- max_slave_replication_lag=120");




    Test->CloseMaxscaleConn();


    global_result += CheckMaxscaleAlive();

    Test->Copy_all_logs();
    return global_result;
}


