/**
 * @file session_limits.cpp - test for 'max_sescmd_history' and 'connection_timeout'
 * - add follwoling to router configuration
 * @verbatim
connection_timeout=30
router_options=max_sescmd_history=10
@endverbatim
 * - open session
 * - wait 20 seconds, check if session is alive, expect ok
 * - wait 20 seconds more, check if session is alive, expect failure
 * - open new session
 * - execute 10 session commands
 * - check if session is alive, expect ok
 * - execute one more session commad, excpect failure
 */


#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int i;
    char sql[256];

    Test->read_env();
    Test->print_env();

    printf("Open session and wait 20 seconds\n");
    Test->connect_maxscale();
    sleep(20);
    printf("Execute query to check session");
    global_result += execute_query(Test->conn_rwsplit, "SELECT 1");
    printf("Wait 20 seconds more and try quiry again expecting failure\n");
    if (execute_query(Test->conn_rwsplit, "SELECT 1") == 0) {
        printf("Session was not closed after 40 seconds\n");
        global_result++;
    }
    Test->close_maxscale_connections();

    printf("Open session and execute 10 session commands\n");fflush(stdout);
    Test->connect_maxscale();
    for (i = 0; i < 10; i++) {
        sprintf(sql, "set @test=%d", i);
        global_result += execute_query(Test->conn_rwsplit, sql);
    }
    printf("done!\n");

    printf("Execute one moe session command and expect failure\n");
    if (execute_query(Test->conn_rwsplit, "set @test=11") == 0) {
        printf("Session was not closed after 10 session commands\n");
        global_result++;
    }
    Test->close_maxscale_connections();


    Test->copy_all_logs(); return(global_result);
}
