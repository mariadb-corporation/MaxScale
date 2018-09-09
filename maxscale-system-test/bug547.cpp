/**
 * @file bug547.cpp regression case for bug 547 and bug 594 ( "get_dcb fails if slaves are not available" and
 *"Maxscale fails to start without anything in the logs if there is no slave available" )
 * Behaviour has been changed and this test check only for crash
 * - block all slaves
 * - try some queries (create table, do INSERT using RWSplit router)
 * - check there is no crash
 */

/*
 *  Vilho Raatikka 2014-09-16 07:43:54 UTC
 *  get_dcb function returns the backend descriptor for router. Some merge has broken the logic and in case of
 * non-existent slave the router simply fails to find a backend server although master would be available.
 *  Comment 1 Vilho Raatikka 2014-09-16 09:40:14 UTC
 *  get_dcb now searches master if slaves are not available.
 */

// also relates to bug594
// all slaves in MaxScale config have wrong IP

#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    for (int i = 1; i < test.repl->N; i++)
    {
        test.set_timeout(20);
        test.repl->block_node(i);
    }

    test.set_timeout(30);
    test.maxscales->wait_for_monitor();

    test.set_timeout(30);
    test.tprintf("Connecting to all MaxScale services, expecting no errors");
    test.assert(test.maxscales->connect_maxscale(0) == 0, "Connection should not fail");

    test.set_timeout(30);
    test.tprintf("Trying some queries, expecting no failures");
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS t1");
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE TABLE t1 (x INT)");
    test.try_query(test.maxscales->conn_rwsplit[0], "INSERT INTO t1 (x) VALUES (1)");
    test.try_query(test.maxscales->conn_rwsplit[0], "select * from t1");
    test.try_query(test.maxscales->conn_master[0], "select * from t1");
    test.try_query(test.maxscales->conn_slave[0], "select * from t1");

    test.set_timeout(10);
    test.maxscales->close_maxscale_connections(0);

    test.set_timeout(30);
    test.repl->unblock_all_nodes();

    test.stop_timeout();
    test.check_log_err(0, "fatal signal 11", false);

    return test.global_result;
}
