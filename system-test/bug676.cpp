/**
 * @file bug676.cpp  reproducing attempt for bug676
 * - connect to RWSplit
 * - stop node0
 * - sleep 20 seconds
 * - reconnect
 * - check if 'USE test' is ok
 * - check MaxScale is alive
 */

#include <iostream>
#include <maxtest/testconnections.hh>
#include <maxtest/galera_cluster.hh>

int main(int argc, char* argv[])
{
    TestConnections::require_galera(true);
    TestConnections test(argc, argv);

    test.reset_timeout();

    test.maxscale->connect_maxscale();
    test.tprintf("Stopping node 0");
    test.galera->block_node(0);
    test.maxscale->close_maxscale_connections();

    test.tprintf("Waiting until the monitor picks a new master");
    test.maxscale->wait_for_monitor();

    test.reset_timeout();

    test.maxscale->connect_maxscale();
    test.try_query(test.maxscale->conn_rwsplit[0], "USE test");
    test.try_query(test.maxscale->conn_rwsplit[0], "show processlist;");
    test.maxscale->close_maxscale_connections();

    test.galera->unblock_node(0);

    return test.global_result;
}
