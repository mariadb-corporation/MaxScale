/**
 * @file session_limits.cpp - test for 'max_sescmd_history' and 'connection_timeout' parameters
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
    if (execute_query(Test->conn_rwsplit, "SELECT 1") == 0)
    {
        Test->add_result(1, "Session was not closed after 40 seconds\n");
    }
    Test->close_maxscale_connections();

    Test->tprintf("Open session and execute 10 session commands\n");
    fflush(stdout);
    Test->connect_maxscale();
    for (i = 0; i < 10; i++)
    {
        sprintf(sql, "set @test=%d", i);
        Test->try_query(Test->conn_rwsplit, sql);
    }
    Test->tprintf("done!\n");

    Test->tprintf("Execute one more session command and expect message in error log\n");
    execute_query(Test->conn_rwsplit, "set @test=11");
    sleep(5);
    Test->check_log_err((char *) "Router session exceeded session command history limit", true);
    Test->close_maxscale_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
