/*
 * Copyright (c) 2023 MariaDB plc
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
#include <exception>

namespace
{
void drop_connections(TestConnections& test)
{
    test.check_maxctrl("set server --force server1 maintenance");
    test.check_maxctrl("clear server server1 maintenance");
}
}

void test_main(TestConnections& test)
{
    test.check_maxctrl("stop monitor MariaDB-Monitor");
    auto c = test.maxscale->rwsplit();
    auto srv = test.repl->get_connection(0);
    auto lock_conn = test.repl->get_connection(0);
    std::string lock_sql = "SELECT GET_LOCK('mxs4549_splice_results', 300)";
    std::string unlock_sql = "SELECT RELEASE_LOCK('mxs4549_splice_results')";

    auto lock = [&](){
        MXT_EXPECT(lock_conn.connect());
        MXT_EXPECT(lock_conn.query(lock_sql));
    };

    auto unlock = [&](){
        MXT_EXPECT(lock_conn.query(unlock_sql));
        lock_conn.disconnect();
    };

    test.log_printf("Sanity check");
    lock();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("START TRANSACTION"));
    MXT_EXPECT(c.send_query("SELECT 1; " + lock_sql));
    MXT_EXPECT(c.read_query_result());
    drop_connections(test);
    unlock();
    MXT_EXPECT(c.read_query_result());
    MXT_EXPECT(c.query("COMMIT"));
    c.disconnect();

    test.log_printf("Multiple queries in transaction");
    lock();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("START TRANSACTION"));
    MXT_EXPECT(c.query("SELECT 1"));
    MXT_EXPECT(c.send_query("SELECT 2; " + lock_sql));
    MXT_EXPECT(c.read_query_result());
    drop_connections(test);
    unlock();
    MXT_EXPECT(c.read_query_result());
    MXT_EXPECT(c.query("SELECT 3"));
    MXT_EXPECT(c.query("COMMIT"));
    c.disconnect();

    test.log_printf("Large result in interrupted query");
    lock();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("START TRANSACTION"));
    MXT_EXPECT(c.send_query("SELECT seq from test.seq_0_to_100000; " + lock_sql));
    MXT_EXPECT(c.read_query_result());
    drop_connections(test);
    unlock();
    MXT_EXPECT(c.read_query_result());
    MXT_EXPECT(c.query("COMMIT"));
    c.disconnect();

    test.log_printf("Replay interrupted statement twice");
    lock();
    MXT_EXPECT(lock_conn.query("SELECT GET_LOCK('second_lock', 300)"));
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("START TRANSACTION"));
    MXT_EXPECT(c.send_query("SELECT 1; " + lock_sql + "; SELECT SLEEP(5); SELECT 2;"));
    MXT_EXPECT(c.read_query_result());
    drop_connections(test);
    unlock();
    // One result was read, read one more and block the node after that. The statement should get replayed
    // again on the same server and the result should be discarded.
    MXT_EXPECT(c.read_query_result());
    drop_connections(test);
    MXT_EXPECT(c.read_query_result());
    MXT_EXPECT(c.read_query_result());
    MXT_EXPECT(c.query("COMMIT"));
    c.disconnect();

    test.log_printf("Non-deterministic value in trailing part of a replayed statement");
    lock();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("START TRANSACTION"));
    // Trailing UUID() should not affect the result
    MXT_EXPECT(c.send_query("SELECT 1; " + lock_sql + "; SELECT UUID()"));
    MXT_EXPECT(c.read_query_result());
    drop_connections(test);
    unlock();
    MXT_EXPECT(c.read_query_result());
    MXT_EXPECT(c.read_query_result());
    MXT_EXPECT(c.query("COMMIT"));
    c.disconnect();

    test.log_printf("Non-deterministic value in leading part of a replayed statement");
    lock();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("START TRANSACTION"));
    // The interrupted query should fail due to a checksum mismatch
    MXT_EXPECT(c.send_query("SELECT UUID(); " + lock_sql + "; SELECT 1"));
    MXT_EXPECT(c.read_query_result());
    drop_connections(test);
    unlock();
    MXT_EXPECT(!c.read_query_result());
    MXT_EXPECT(!c.read_query_result());
    MXT_EXPECT(!c.query("COMMIT"));
    c.disconnect();

    test.log_printf("Non-deterministic value in a previous statement");
    lock();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("START TRANSACTION"));
    MXT_EXPECT(c.query("SELECT UUID()"));
    MXT_EXPECT(c.send_query("SELECT 1; " + lock_sql));
    MXT_EXPECT(c.read_query_result());
    drop_connections(test);
    unlock();
    MXT_EXPECT(!c.read_query_result());
    MXT_EXPECT(!c.read_query_result());
    MXT_EXPECT(!c.query("COMMIT"));
    c.disconnect();

    auto server_conn = test.repl->backend(0)->open_connection();
    auto table = server_conn->create_table("test.conflict", "x INT PRIMARY KEY, data INT");
    server_conn->cmd("INSERT INTO test.conflict VALUES (0, 0), (1, 1)");

    test.log_printf("Replay partially delivered result that ends in a deadlock error");
    auto c2 = test.maxscale->rwsplit();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c2.connect());
    MXT_EXPECT(c.query("START TRANSACTION"));
    MXT_EXPECT(c2.query("START TRANSACTION"));
    MXT_EXPECT(c.query("UPDATE test.conflict SET data = data + 1 WHERE x = 0"));
    MXT_EXPECT(c2.query("UPDATE test.conflict SET data = data + 1 WHERE x = 1"));
    // The update in this multi-statement will get a deadlock error for the UPDATE
    MXT_EXPECT(c2.send_query("SELECT 2; SELECT SLEEP(2); "
                             "UPDATE test.conflict SET data = data + 1 WHERE x = 0"));
    MXT_EXPECT(c.query("UPDATE test.conflict SET data = data + 1 WHERE x = 1"));
    MXT_EXPECT(c.query("COMMIT"));
    MXT_EXPECT(c2.read_query_result());
    MXT_EXPECT(c2.read_query_result());
    MXT_EXPECT(c2.read_query_result());
    MXT_EXPECT(c2.query("COMMIT"));
    c.disconnect();
    c2.disconnect();

    test.log_printf("Replayed result is shorted than original");
    lock();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(srv.connect());
    MXT_EXPECT(c.query("START TRANSACTION"));
    MXT_EXPECT(c.send_query("SELECT * FROM test.conflict; " + lock_sql));
    MXT_EXPECT(c.read_query_result());
    // Start a TRUNCATE command. It'll be blocked by the open transaction.
    MXT_EXPECT(srv.send_query("TRUNCATE TABLE test.conflict"));
    drop_connections(test);
    unlock();
    MXT_EXPECT(srv.read_query_result());
    MXT_EXPECT(!c.read_query_result());
    MXT_EXPECT(!c.query("COMMIT"));
    c.disconnect();
    srv.disconnect();
    server_conn->cmd("INSERT INTO test.conflict VALUES (0, 0), (1, 1)");

    test.log_printf("Replayed result is longer than original");
    lock();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(srv.connect());
    MXT_EXPECT(c.query("START TRANSACTION"));
    MXT_EXPECT(c.send_query("SELECT * FROM test.conflict; " + lock_sql));
    MXT_EXPECT(c.read_query_result());
    MXT_EXPECT(srv.send_query("INSERT INTO test.conflict VALUES (2, 2)"));
    drop_connections(test);
    unlock();
    MXT_EXPECT(srv.read_query_result());
    MXT_EXPECT(!c.read_query_result());
    MXT_EXPECT(!c.query("COMMIT"));
    c.disconnect();
    server_conn->cmd("DELETE FROM test.conflict WHERE x = 2");
}

int main(int argc, char** argv)
{
    return TestConnections{}.run_test(argc, argv, test_main);
}
