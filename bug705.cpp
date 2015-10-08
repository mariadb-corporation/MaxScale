/**
 * @file bug705.cpp regression case for bug 705 ("Authentication fails when the user connects to a database when the SQL mode includes ANSI_QUOTES")
 *
 * - use only one backend
 * - derectly to backend SET GLOBAL sql_mode="ANSI"
 * - restart MaxScale
 * - check log for "Error : Loading database names for service RW_Split encountered error: Unknown column"
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    printf("Connecting to backend %s\n", Test->repl->IP[0]);  fflush(stdout);
    Test->repl->connect();

    Test->tprintf("Sending SET GLOBAL sql_mode=\"ANSI\" to backend %s\n", Test->repl->IP[0]);
    execute_query(Test->repl->nodes[0], "SET GLOBAL sql_mode=\"ANSI\"");

    Test->repl->close_connections();

    Test->tprintf("Restarting MaxScale\n");

    pid_t pid = fork();
    if (!pid) {
        Test->restart_maxscale();
        fflush(stdout);
    } else {

        Test->tprintf("Waiting 20 seconds\n");
        sleep(20);

        Test->check_log_err((char *) "Error : Loading database names", FALSE);
        Test->check_log_err((char *) "error: Unknown column", FALSE);

        Test->copy_all_logs(); return(Test->global_result);
    }
}

