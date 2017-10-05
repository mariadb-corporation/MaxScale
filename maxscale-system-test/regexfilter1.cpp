/**
 * @file regexfilter1.cpp Simple regexfilter tests; aslo regression case for mxs508 ("regex filter ignores username")
 *
 * Three services are configured with regexfilter, each with different parameters.
 * All services are queried with SELECT 123. The first service should replace it
 * with SELECT 0 and the second and third services should not replace it.
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * test = new TestConnections(argc, argv);
    test->connect_maxscale();
    test->add_result(execute_query_check_one(test->maxscales->conn_rwsplit[0], "SELECT 123", "0"),
                     "Query to first service should have replaced the query.\n");
    test->add_result(execute_query_check_one(test->maxscales->conn_slave[0], "SELECT 123", "123"),
                     "Query to second service should not have replaced the query.\n");
    test->add_result(execute_query_check_one(test->maxscales->conn_master[0], "SELECT 123", "123"),
                     "Query to third service should not have replaced the query.\n");
    test->close_maxscale_connections();
    int rval = test->global_result;
    delete test;
    return rval;
}
