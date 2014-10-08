#include <iostream>
#include "testconnections.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();
    Test->ReadEnv();
    Test->PrintIP();
    //Test->galera->Connect();
    Test->repl->Connect();
    //Test->ConnectMaxscale();
    Test->repl->ChangeMaster(2);
}

