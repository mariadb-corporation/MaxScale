
#include <iostream>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    int OldMaster;
    int NewMaster;

    if (argc != 3)
    {
        printf("Usage: change_master NewMasterNode OldMasterNode\n");
        exit(1);
    }
    TestConnections * Test = new TestConnections(argc, argv);

    sscanf(argv[1], "%d", &NewMaster);
    sscanf(argv[2], "%d", &OldMaster);

    Test->tprintf("Changing master from node %d (%s) to node %d (%s)\n", OldMaster, Test->repl->IP[OldMaster],
                  NewMaster, Test->repl->IP[NewMaster]);

    Test->repl->connect();
    Test->repl->change_master(NewMaster, OldMaster);
    Test->repl->close_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
