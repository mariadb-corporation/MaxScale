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

#include <maxtest/blob_test.hh>
#include <maxtest/testconnections.hh>

namespace
{
void test_main(TestConnections& test)
{
    TestConnections* Test = &test;
    auto& repl = *test.repl;
    auto& mxs = *test.maxscale;

    repl.execute_query_all_nodes("set global max_allowed_packet=67108864");

    struct TestCase
    {
        std::string   name;
        unsigned long chunk_size;
        int           chunks;
        int           rows;
    };

    TestCase tests[] = {
        {"LONGBLOB",   1000000, 20, 1},
        {"BLOB",       1000,    8,  1},
        {"MEDIUMBLOB", 1000000, 2,  1}
    };

    mxs.connect_rwsplit();
    mxs.connect_readconn_master();

    for (auto& t : tests)
    {
        test.tprintf("%s: RWSplit", t.name.c_str());
        test_longblob(test, mxs.conn_rwsplit, t.name.c_str(), t.chunk_size, t.chunks, t.rows);
        test.tprintf("%s: ReadConn master", t.name.c_str());
        test_longblob(test, mxs.conn_master, t.name.c_str(), t.chunk_size, t.chunks, t.rows);

        if (!test.ok())
        {
            break;
        }
    }

    auto conn = repl.backend(0)->open_connection();
    conn->cmd("DROP TABLE test.long_blob_table");
}
}
int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
