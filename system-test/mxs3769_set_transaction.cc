/*
 * Copyright (c) 2022 MariaDB Corporation Ab
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
 * MXS-3769: Support for SET TRANSACTION
 *
 * https://jira.mariadb.org/browse/MXS-3769
 */

#include <maxtest/testconnections.hh>

void mxs4734(TestConnections& test)
{
    test.check_maxctrl("alter service RWS transaction_replay=true transaction_replay_timeout=120s");
    auto rws = test.maxscale->rwsplit();
    test.expect(rws.connect(), "Failed to connect: %s", rws.error());

    test.expect(rws.query("CREATE OR REPLACE TABLE test.t1(id INT)"),
                "CREATE should not fail: %s", rws.error());
    test.expect(rws.query("SET TRANSACTION READ ONLY"), "SET TRANSACTION failed: %s", rws.error());
    test.expect(rws.query("START TRANSACTION"), "START TRANSACTION failed: %s", rws.error());
    test.expect(!rws.query("INSERT INTO test.t1 VALUES (1)"), "INSERT should fail");

    test.repl->block_node(0);
    test.maxscale->wait_for_monitor(2);
    test.repl->unblock_node(0);
    test.maxscale->wait_for_monitor(2);

    test.expect(rws.query("SELECT 1"), "SELECT should work: %s", rws.error());
    test.expect(!rws.query("INSERT INTO test.t1 VALUES (1)"), "Second INSERT should fail");
    test.expect(rws.query("COMMIT"), "COMMIT should work: %s", rws.error());

    test.check_maxctrl("alter service RWS transaction_replay=false transaction_replay_timeout=0s");
}

#define TRX(a, b) do_trx(a, b, __LINE__)

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect();
    std::string master = test.repl->get_server_id_str(0);

    auto rws = test.maxscale->rwsplit();
    test.expect(rws.connect(), "Failed to connect: %s", rws.error());
    test.expect(rws.query("CREATE OR REPLACE TABLE t1(id INT)"), "CREATE failed: %s", rws.error());

    auto do_trx = [&](std::string trx_sql, bool expected, int line) {
        test.expect(rws.query(trx_sql), "at line %d: %s failed: %s", line, trx_sql.c_str(), rws.error());
        test.expect(rws.query("INSERT INTO t1 VALUES (1)") == expected,
                    "at line %d, INSERT %s: %s", line, expected ? "failed" : "succeeded", rws.error());
        test.expect(rws.query("COMMIT"), "at line %d, COMMIT failed: %s", line, rws.error());
    };

    // SET TRANSACTION affects only the next transaction. The INSERT should fail but in the subsequent
    // transaction it should work.
    rws.query("SET TRANSACTION READ ONLY");
    TRX("START TRANSACTION", false);
    TRX("START TRANSACTION", true);

    // Changing the default access mode should cause transactions to be routed to slave servers unless an
    // explicit READ WRITE transaction is used.
    rws.query("SET SESSION TRANSACTION READ ONLY");
    sleep(2);
    TRX("START TRANSACTION", false);
    TRX("START TRANSACTION", false);
    TRX("START TRANSACTION READ WRITE ", true);
    TRX("START TRANSACTION READ WRITE", true);

    // Setting the access mode to READ WRITE while the session default is READ ONLY should cause the next
    // transaction to be routed to the master server.
    rws.query("SET TRANSACTION READ WRITE");
    TRX("START TRANSACTION", true);
    TRX("START TRANSACTION", false);

    // Changing the default back to READ WRITE should make transactions behave normally.
    rws.query("SET SESSION TRANSACTION READ WRITE");
    sleep(2);
    TRX("START TRANSACTION", true);
    TRX("START TRANSACTION", true);

    // SET TRANSACTION READ ONLY should now again only redirect one transaction.
    rws.query("SET TRANSACTION READ ONLY");
    TRX("START TRANSACTION", false);
    TRX("START TRANSACTION", true);

    rws.query("DROP TABLE t1");
    rws.disconnect();

    // MXS-4734: SET TRANSACTION READ ONLY isn't replayed correctly with transaction_replay
    // https://jira.mariadb.org/browse/MXS-4734
    mxs4734(test);

    return test.global_result;
}
