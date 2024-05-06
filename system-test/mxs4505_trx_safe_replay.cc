/*
 * Copyright (c) 2023 MariaDB plc
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

bool do_test(TestConnections& test)
{
    auto c1 = test.maxscale->rwsplit();
    auto c2 = test.maxscale->rwsplit();
    auto r1 = test.repl->get_connection(0);
    test.expect(c1.connect() && c2.connect() && r1.connect(),
                "Connections failed: %s%s%s", c1.error(), c2.error(), r1.error());

    // Get the real connection ID on the master. We'll need to bypass the KILL handling in MaxScale to make
    // sure the transaction replay takes place. Normally, a KILL will disable transaction replay to prevent
    // the killed query from being attempted again.
    auto c2_id = c2.field("SELECT CONNECTION_ID(), @@last_insert_id");
    test.expect(!c2_id.empty(), "CONNECTION_ID() returned an empty result");

    test.log_printf("Create a table on one connection");
    c1.query("CREATE TABLE test.t1(id INT)");

    test.log_printf("Start a transaction and insert a row into it on a second one");
    c2.query("BEGIN");
    c2.query("INSERT INTO test.t1 VALUES (1)");

    test.log_printf("Lock all tables on the first connection");
    c1.query("FLUSH TABLES WITH READ LOCK");

    test.log_printf("Start a COMMIT on the second connection");
    c2.send_query("COMMIT");

    test.log_printf("KILL the second connection and unlock tables");
    test.expect(r1.query("KILL " + c2_id), "KILL should succeed: %s", r1.error());
    c1.query("UNLOCK TABLES");

    test.log_printf("Read the result of the COMMIT");
    bool ok = c2.read_query_result();

    test.log_printf("Drop the table");
    c1.query("DROP TABLE test.t1");

    return ok;
}

void test_main(TestConnections& test)
{
    test.log_printf("1. The commit should not be replayed by default.");
    test.expect(!do_test(test), "COMMIT should fail");

    test.log_printf("2. With transaction_replay_safe_commit off, the replay should succeed");
    test.maxctrl("alter service RW-Split-Router transaction_replay_safe_commit=false");
    test.expect(do_test(test), "COMMIT should work");
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
