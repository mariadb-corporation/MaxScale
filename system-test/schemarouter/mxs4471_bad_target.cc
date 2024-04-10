/*
 * Copyright (c) 2021 MariaDB Corporation Ab
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

#include <maxtest/testconnections.hh>

void block_and_query(TestConnections& test, Connection& c, int i, int last)
{
    test.repl->block_node(i);
    test.maxscale->wait_for_monitor(2);

    test.log_printf("Node %d blocked, routing query", i);
    auto num = c.field("SELECT COUNT(*) FROM test.t1");

    if (i == last)
    {
        // Since the schemarouter does not reconnect to the nodes, the query on the last node
        // will return an error.
        test.expect(num.empty(), "Query on final node should return an error but it returned %s",
                    num.c_str());
    }
    else
    {
        test.expect(num == "1", "Table on node %d should have one row (error %s)", i, c.error());
    }

    test.log_printf("Unblocking node %d", i);
    test.repl->unblock_node(i);
    test.maxscale->wait_for_monitor(2);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    auto repl = test.repl->get_connection(0);
    repl.connect();
    repl.query("CREATE OR REPLACE TABLE test.t1(id INT)");
    repl.query("INSERT INTO test.t1 VALUES (1)");
    test.repl->sync_slaves();

    auto c = test.maxscale->rwsplit();

    // The node selection used to return the first value from a std::set<mxs::Target*>. This means that the
    // value was not deterministic and thus the test must be repeated in the inverse iteration order to make
    // sure all nodes have failed while a functional candidate was still available.
    c.connect();

    for (int i = 0; i < test.repl->N; i++)
    {
        block_and_query(test, c, i, test.repl->N - 1);
    }

    c.disconnect();
    c.connect();

    for (int i = test.repl->N - 1; i >= 0; i--)
    {
        block_and_query(test, c, i, 0);
    }

    repl.connect();
    repl.query("DROP TABLE test.t1");
    return test.global_result;
}
