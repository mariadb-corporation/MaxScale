/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file regexfilter1.cpp Simple regexfilter tests; aslo regression case for mxs508 ("regex filter ignores
 * username")
 *
 * Three services are configured with regexfilter, each with different parameters.
 * All services are queried with SELECT 123. The first service should replace it
 * with SELECT 0 and the second and third services should not replace it.
 */


#include <iostream>
#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections* test = new TestConnections(argc, argv);
    test->maxscale->connect_maxscale();
    test->add_result(execute_query_check_one(test->maxscale->conn_rwsplit, "SELECT 123", "0"),
                     "Query to first service should have replaced the query.\n");
    test->add_result(execute_query_check_one(test->maxscale->conn_slave, "SELECT 123", "123"),
                     "Query to second service should not have replaced the query.\n");
    test->add_result(execute_query_check_one(test->maxscale->conn_master, "SELECT 123", "123"),
                     "Query to third service should not have replaced the query.\n");
    test->maxscale->close_maxscale_connections();
    int rval = test->global_result;
    delete test;
    return rval;
}
