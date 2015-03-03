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
    int global_result = 0;
    int i;

    Test->read_env();
    Test->print_env();

    printf("Connecting to backend %s\n", Test->repl->IP[0]);  fflush(stdout);
    Test->repl->connect();

    printf("Sending SET GLOBAL sql_mode=\"ANSI\" to backend %s\n", Test->repl->IP[0]); fflush(stdout);
    execute_query(Test->repl->nodes[0], "SET GLOBAL sql_mode=\"ANSI\"");

    Test->repl->close_connections();

    char sys1[4096];

    printf("Restarting MaxScale\n");  fflush(stdout);

    pid_t pid = fork();
    if (!pid) {
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"service maxscale restart &\"", Test->maxscale_sshkey, Test->maxscale_IP);
        printf("%s\n", sys1); fflush(stdout);
        system(sys1); fflush(stdout);
    } else {

        printf("Waiting 20 seconds\n"); fflush(stdout);
        sleep(20);

        global_result += check_log_err((char *) "Error : Loading database names", FALSE);
        global_result += check_log_err((char *) "error: Unknown column", FALSE);


        Test->copy_all_logs(); return(global_result);
    }
}

