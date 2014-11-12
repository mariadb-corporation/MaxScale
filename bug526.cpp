#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    int global_result = CheckLogErr((char *) "Error : Unable to find library for module: foobar", TRUE);
    global_result += CheckLogErr((char *) "Failed to create filter 'testfilter' for service", TRUE);
    global_result += CheckLogErr((char *) "Error : Failed to create RW Split Router session", TRUE);
    TestConnections * Test = new TestConnections();
    Test->ReadEnv();
    Test->PrintIP();
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
    return(global_result);
}
