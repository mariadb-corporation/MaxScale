
#include <my_config.h>
#include <iostream>
#include "testconnections.h"

using namespace std;


int main(int argc, char *argv[])
{
    int global_result = 0;
    int OldMaster;
    int NewMaster;

    if (argc !=3) {
        printf("Usage: change_master NewMasterNode OldMasterNode\n");
        exit(1);
    }
    TestConnections * Test = new TestConnections(argv[0]);
    Test->ReadEnv();
    Test->PrintIP();

    sscanf(argv[1], "%d", &NewMaster);
    sscanf(argv[2], "%d", &OldMaster);

    printf("Changing master from node %d (%s) to node %d (%s)\n", OldMaster, Test->repl->IP[OldMaster], NewMaster, Test->repl->IP[NewMaster]);

    Test->repl->Connect();
    Test->repl->ChangeMaster(NewMaster, OldMaster);
    Test->repl->CloseConn();

    return(global_result);
}
