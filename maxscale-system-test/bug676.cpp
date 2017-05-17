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
#include "testconnections.h"
#include "mariadb_func.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.set_timeout(30);

    test.connect_maxscale();
    test.tprintf("Stopping node 0");
    test.galera->block_node(0);
    test.close_maxscale_connections();

    test.stop_timeout();

    test.tprintf("Waiting until the monitor picks a new master");
    sleep(20);

    test.set_timeout(30);

    test.connect_maxscale();
    test.try_query(test.conn_rwsplit, "USE test");
    test.try_query(test.conn_rwsplit, "show processlist;");
    test.close_maxscale_connections();

    test.stop_timeout();

    test.galera->unblock_node(0);

    return test.global_result;
}

