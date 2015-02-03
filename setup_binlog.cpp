/**
 * @file setup_binlog regression case for bug 705 ("Authentication fails when the user connects to a database when the SQL mode includes ANSI_QUOTES")
 *
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main()
{
    TestConnections * Test = new TestConnections();
    //global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    Test->repl->StartBinlog(Test->Maxscale_IP, 5306);

}


