/**
 * @file bug526.cpp regression case for bug 526 ( " Wrong module name crashes maxscale on connect" )
 *
 * - Maxscale.cnf with "filters=QLA|testfilter" for RWSplit router service, 'testfilter' is not defined.
 * - check error log for proper error messages and checks if ReadConn services are alive
 */

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    int global_result = 0;

    TestConnections * Test = new TestConnections(argv[0]);
    Test->ReadEnv();
    Test->PrintIP();

    Test->ConnectRWSplit();
    sleep(5);
    global_result += CheckLogErr((char *) "Error : Unable to find library for module: foobar", TRUE);
    global_result += CheckLogErr((char *) "Failed to create filter 'testfilter' for service", TRUE);
    global_result += CheckLogErr((char *) "Error : Setting up filters failed. Terminating session RW Split Router", TRUE);

    printf("Trying ReaConn master\n");
    if (Test->ConnectReadMaster() != 0) {
        global_result++;
        printf("Error connection to ReadConn master\n");
    }
    printf("Trying ReaConn slave\n");
    if (Test->ConnectReadSlave() != 0) {
        global_result++;
        printf("Error connection to ReadConn slave\n");
    }
    Test->CloseReadMaster();
    Test->CloseReadSlave();
    Test->Copy_all_logs(); return(global_result);
}
