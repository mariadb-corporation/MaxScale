/*
 * Copyright (c) 2021 MariaDB Corporation Ab
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

#include <maxtest/testconnections.hh>
#include <sstream>

void test_trx(TestConnections& test, const char* before, const char* after)
{
    for (int i = 0; i < test.repl->N; i++)
    {
        auto n = test.repl->get_connection(i);
        n.connect();
        n.query("CREATE OR REPLACE TABLE test.t" + std::to_string(i) + "(id INT)");
    }

    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());
    test.expect(c.query(before), "Failed to start transaction with '%s': %s", before, c.error());

    for (int i = 0; i < test.repl->N; i++)
    {
        std::ostringstream ss;
        ss << "INSERT INTO test.t" << i << " VALUES (" << i << ")";
        test.expect(c.query(ss.str()), "Failed to insert into node %d: %s", i, c.error());
    }

    test.expect(c.query(after), "Failed to end transaction with '%s': %s", after, c.error());

    // To make sure that the COMMIT actually ends up being executed successfully on all nodes, we need to do a
    // read on each shard to check that the values are there. The latest participating shard in the
    // transaction returns the response to the client. This guarantees that transactions that only use one
    // shard will always be successfully committed if MaxScale returns an OK packet to the client.
    for (int i = 0; i < test.repl->N; i++)
    {
        auto num = c.field("SELECT COUNT(id) FROM test.t" + std::to_string(i));
        test.expect(num == "1",
                    "Expected 1 row on node %d before reconnection but got '%s' (error: %s)",
                    i, num.c_str(), c.error());
    }

    c.disconnect();
    test.expect(c.connect(), "Failed to reconnect: %s", c.error());

    for (int i = 0; i < test.repl->N; i++)
    {
        auto num = c.field("SELECT COUNT(id) FROM test.t" + std::to_string(i));
        test.expect(num == "1",
                    "Expected 1 row on node %d after reconnection but got '%s' (error: %s)",
                    i, num.c_str(), c.error());
    }

    c.disconnect();

    for (int i = 0; i < test.repl->N; i++)
    {
        auto n = test.repl->get_connection(i);
        n.connect();
        n.query("DROP TABLE test.t" + std::to_string(i) + " (id INT)");
    }
}

void test_rollback(TestConnections& test, const char* before)
{
    auto n = test.repl->get_connection(3);
    n.connect();
    n.query("CREATE OR REPLACE TABLE test.testing_rollback(id INT)");

    // We're creating a new table. We need to wait for the cached shard map to go stale in order for the query
    // to get routed correctly.
    sleep(2);

    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());
    test.expect(c.query(before), "Failed to start transaction with '%s': %s", before, c.error());
    test.expect(c.query("INSERT INTO test.testing_rollback VALUES (1)"),
                "Failed to insert: %s", c.error());
    test.expect(c.query("ROLLBACK"), "Failed to rollback: %s", c.error());

    auto num = c.field("SELECT COUNT(*) FROM test.testing_rollback");
    test.expect(num == "0", "Table test.testing_rollback should be empty "
                            "but it has '%s' rows (error: %s)", num.c_str(), c.error());
    c.disconnect();

    n.query("DROP TABLE test.testing_rollback");
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->execute_query_all_nodes("STOP SLAVE; RESET SLAVE;");

    test.log_printf("Testing BEGIN and COMMIT");
    test_trx(test, "BEGIN", "COMMIT");

    test.log_printf("Testing SET autocommit=0 and COMMIT");
    test_trx(test, "SET autocommit=0", "COMMIT");

    test.log_printf("Testing SET autocommit=0 and SET autocommit=1");
    test_trx(test, "SET autocommit=0", "SET autocommit=1");

    test.log_printf("Testing BEGIN and ROLLBACK");
    test_rollback(test, "BEGIN");

    test.log_printf("Testing SET autocommit=0 and ROLLBACK");
    test_rollback(test, "SET autocommit=0");

    return test.global_result;
}
