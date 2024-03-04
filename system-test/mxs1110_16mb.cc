/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file mxs1110_16mb.cpp - trying to use LONGBLOB with > 16 mb data blocks
 * - try to insert large LONGBLOB via RWSplit in blocks > 16mb
 * - read data via RWsplit, ReadConn master, ReadConn slave, compare with inserted data
 */

#include <maxtest/testconnections.hh>
#include <maxtest/blob_test.hh>

using std::string;

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    int chunk_size = 2500000;
    int chunk_num = 5;
    int rows = 2;

    repl.execute_query_all_nodes("set global max_allowed_packet=200000000");
    mxs.connect_maxscale();
    repl.connect();
    test.tprintf("LONGBLOB: Trying send data via RWSplit");
    test_longblob(test, mxs.conn_rwsplit, "LONGBLOB", chunk_size, chunk_num, rows);
    repl.close_connections();
    mxs.close_maxscale_connections();

    repl.sync_slaves();
    mxs.connect_maxscale();
    test.tprintf("Checking data via RWSplit");
    check_longblob_data(test, mxs.conn_rwsplit, chunk_size, chunk_num, rows);
    test.tprintf("Checking data via ReadConn master");
    check_longblob_data(test, mxs.conn_master, chunk_size, chunk_num, rows);
    test.tprintf("Checking data via ReadConn slave");
    check_longblob_data(test, mxs.conn_slave, chunk_size, chunk_num, rows);
    mxs.close_maxscale_connections();
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
