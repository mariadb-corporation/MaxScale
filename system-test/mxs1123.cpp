/**
 * MXS-1123: connect_timeout setting causes frequent disconnects
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.maxscales->connect_maxscale();

    test.tprintf("Waiting one second between queries, all queries should succeed");

    sleep(1);
    test.try_query(test.maxscales->conn_rwsplit[0], "select 1");
    sleep(1);
    test.try_query(test.maxscales->conn_master[0], "select 1");
    sleep(1);
    test.try_query(test.maxscales->conn_slave, "select 1");

    test.maxscales->close_maxscale_connections();
    return test.global_result;
}
