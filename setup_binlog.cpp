/**
 * @file setup_binlog regression case for bug 705 ("Authentication fails when the user connects to a database when the SQL mode includes ANSI_QUOTES")
 *
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    Test->repl->StartBinlog(Test->Maxscale_IP, 5306);

    global_result += executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show servers");

    return(global_result);
}


