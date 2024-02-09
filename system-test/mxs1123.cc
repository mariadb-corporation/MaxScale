/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-1123: connect_timeout setting causes frequent disconnects
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.maxscale->connect_maxscale();

    test.tprintf("Waiting one second between queries, all queries should succeed");

    sleep(1);
    test.try_query(test.maxscale->conn_rwsplit, "select 1");
    sleep(1);
    test.try_query(test.maxscale->conn_master, "select 1");
    sleep(1);
    test.try_query(test.maxscale->conn_slave, "select 1");

    test.maxscale->close_maxscale_connections();
    return test.global_result;
}
