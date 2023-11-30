/*
 * Copyright (c) 2022 MariaDB Corporation Ab
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

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Connection failed: %s", c.error());

    auto OK = [&](const char* query) {
            test.expect(c.query(query), "Query '%s' failed: %s", query, c.error());
        };

    auto ERR = [&](const char* query) {
            test.expect(!c.query(query), "Query '%s' should fail", query);
        };

    OK("CREATE OR REPLACE TABLE test.t1(id INT AUTO_INCREMENT PRIMARY KEY)");

    test.tprintf("transaction_replay_checksum=no_insert_id");

    OK("START TRANSACTION");
    OK("INSERT INTO test.t1 VALUES ()");
    OK("SELECT LAST_INSERT_ID()");
    OK("SELECT @@last_insert_id");
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor(2);
    test.repl->unblock_node(0);
    OK("COMMIT");

    c.disconnect();
    test.check_maxctrl("alter service RW-Split-Router transaction_replay_checksum=result_only");
    test.expect(c.connect(), "Second connection failed: %s", c.error());

    test.tprintf("transaction_replay_checksum=result_only");

    OK("START TRANSACTION");
    OK("INSERT INTO test.t1 VALUES ()");
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor(2);
    test.repl->unblock_node(0);
    OK("COMMIT");

    OK("START TRANSACTION");
    OK("INSERT INTO test.t1 VALUES ()");
    OK("SELECT LAST_INSERT_ID()");
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor(2);
    test.repl->unblock_node(0);
    ERR("COMMIT");

    test.check_maxctrl("alter service RW-Split-Router transaction_replay_checksum=full");
    test.expect(c.connect(), "Third connection failed: %s", c.error());

    test.tprintf("transaction_replay_checksum=full");

    OK("START TRANSACTION");
    OK("INSERT INTO test.t1 VALUES ()");
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor(2);
    test.repl->unblock_node(0);
    ERR("COMMIT");

    test.expect(c.connect(), "Final connection failed: %s", c.error());
    OK("DROP TABLE test.t1");

    return test.global_result;
}
