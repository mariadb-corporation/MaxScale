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
    Test->set_timeout(200);
    int i;
    char sql[256];

    Test->tprintf("Open session and wait 20 seconds\n");
    Test->connect_maxscale();
    sleep(20);
    Test->tprintf("Execute query to check session\n");
    Test->try_query(Test->conn_rwsplit, "SELECT 1");

    Test->tprintf("Wait 35 seconds more and try quiry again expecting failure\n");
    sleep(35);
    if (execute_query(Test->conn_rwsplit, "SELECT 1") == 0) {
        Test->add_result(1, "Session was not closed after 40 seconds\n");
    }
    Test->close_maxscale_connections();

    Test->tprintf("Open session and execute 10 session commands\n");fflush(stdout);
    Test->connect_maxscale();
    for (i = 0; i < 10; i++) {
        sprintf(sql, "set @test=%d", i);
        Test->try_query(Test->conn_rwsplit, sql);
    }
    Test->tprintf("done!\n");

    Test->tprintf("Execute one moe session command and expect failure\n");
    if (execute_query(Test->conn_rwsplit, "set @test=11") == 0) {
        Test->add_result(1, "Session was not closed after 10 session commands\n");
    }
    Test->close_maxscale_connections();

    Test->copy_all_logs(); return(Test->global_result);
}
