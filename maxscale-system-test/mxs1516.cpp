/**
 * MXS-1516: existing connection don't change routing, even if master switched
 *
 * https://jira.mariadb.org/browse/MXS-1516
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscales->connect();
    test.try_query(test.maxscales->conn_master[0], "SELECT 1");

    // Change master mid-session
    test.repl->connect();
    test.repl->change_master(1, 0);

    // Give the monitor some time to detect it
    sleep(5);

    test.add_result(execute_query_silent(test.maxscales->conn_master[0], "SELECT 1") == 0,
                    "Query should fail");

    // Change the master back to the original one
    test.repl->change_master(0, 1);

    return test.global_result;
}
