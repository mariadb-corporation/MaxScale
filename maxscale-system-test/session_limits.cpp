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

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);
    int first_sleep = 5;
    int second_sleep = 12;

    test.set_timeout(200);

    test.tprintf("Open session, wait %d seconds and execute a query", first_sleep);
    test.connect_maxscale();
    sleep(first_sleep);
    test.try_query(test.conn_rwsplit, "SELECT 1");

    test.tprintf("Wait %d seconds and execute query, expecting failure", second_sleep);
    sleep(second_sleep);
    test.add_result(execute_query(test.conn_rwsplit, "SELECT 1") == 0,
                    "Session was not closed after %d seconds",
                    second_sleep);
    test.close_maxscale_connections();

    test.tprintf("Open session and execute 10 session commands");
    test.connect_maxscale();
    for (int i = 0; i < 10; i++)
    {
        test.try_query(test.conn_rwsplit, "set @test=1");
    }

    test.tprintf("Execute one more session command and expect message in error log");
    execute_query(test.conn_rwsplit, "set @test=1");
    sleep(1);
    test.check_log_err("Router session exceeded session command history limit", true);
    test.close_maxscale_connections();

    return test.global_result;
}
