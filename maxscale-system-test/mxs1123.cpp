/**
 * MXS-1123: connect_timeout setting causes frequent disconnects
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.connect_maxscale();

    test.tprintf("Waiting one second between queries, all queries should succeed");

    sleep(1);
    test.try_query(test.conn_rwsplit, "select 1");
    sleep(1);
    test.try_query(test.conn_master, "select 1");
    sleep(1);
    test.try_query(test.conn_slave, "select 1");

    test.close_maxscale_connections();
    return test.global_result;
}
