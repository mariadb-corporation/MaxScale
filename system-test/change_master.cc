/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxtest/testconnections.hh>

using namespace std;

int main(int argc, char* argv[])
{
    int OldMaster;
    int NewMaster;

    if (argc != 3)
    {
        printf("Usage: change_master NewMasterNode OldMasterNode\n");
        exit(1);
    }
    TestConnections* Test = new TestConnections(argc, argv);

    sscanf(argv[1], "%d", &NewMaster);
    sscanf(argv[2], "%d", &OldMaster);

    Test->tprintf("Changing master from node %d (%s) to node %d (%s)\n",
                  OldMaster,
                  Test->repl->IP[OldMaster],
                  NewMaster,
                  Test->repl->IP[NewMaster]);

    Test->repl->connect();
    Test->repl->change_master(NewMaster, OldMaster);
    Test->repl->close_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
