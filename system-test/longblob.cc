/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file longblob.cpp - trying to use LONGBLOB
 * - try to insert large BLOB, MEDIUMBLOB and LONGBLOB via RWSplit, ReadConn Master and directly to backend
 */

#include <maxtest/blob_test.hh>
#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    auto& repl = *test.repl;

    repl.execute_query_all_nodes("set global max_allowed_packet=67108864");

    auto run_test_case = [&](const char* blob_type, size_t chunk_size, size_t chunks) {
        auto& mxs = *test.maxscale;
        mxs.connect_maxscale();
        repl.connect();
        test.tprintf("%s, rwsplit, chunk size %lu, chunks %lu", blob_type, chunk_size, chunks);
        test_longblob(test, mxs.conn_rwsplit, blob_type, chunk_size, chunks, 1);
        repl.close_connections();
        mxs.close_maxscale_connections();

        mxs.connect_maxscale();
        repl.connect();
        test.tprintf("%s, readconn master, chunk size %lu, chunks %lu", blob_type, chunk_size, chunks);
        test_longblob(test, mxs.conn_master, blob_type, chunk_size, chunks, 1);
        repl.close_connections();
        mxs.close_maxscale_connections();
    };

    run_test_case("BLOB", 1000, 8);
    run_test_case("MEDIUMBLOB", 1000000, 2);
    run_test_case("LONGBLOB", 1000000, 20);

    repl.connect();
    test.try_query(repl.nodes[0], "DROP TABLE long_blob_table");
    repl.disconnect();

    return test.global_result;
}
