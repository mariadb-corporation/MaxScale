/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-2456: Cap transaction replay attempts
 * https://jira.mariadb.org/browse/MXS-2456
 */

#include <maxtest/testconnections.hh>

#define EXPECT(a) test.expect(a, "%s", "Assertion failed: " #a)

void block_master(TestConnections& test)
{
    mxt::MariaDBServer* master;

    while (!(master = test.get_repl_master()))
    {
        test.maxscale->sleep_and_wait_for_monitor(1, 1);
    }

    test.repl->block_node(master->ind());
    test.maxscale->wait_for_monitor();
    sleep(5);
}

void test_replay_ok(TestConnections& test)
{
    test.log_printf("Do a partial transaction");
    Connection c = test.maxscale->rwsplit();
    EXPECT(c.connect());
    EXPECT(c.query("BEGIN"));
    EXPECT(c.query("SELECT 1"));
    EXPECT(c.query("SELECT SLEEP(15)"));

    test.log_printf("Block the node where the transaction was started");
    block_master(test);

    test.log_printf("Then block the node where the transaction replay is "
                    "attempted before the last statement finishes");
    block_master(test);

    test.log_printf("The next query should succeed as we do two replay attempts");
    test.expect(c.query("SELECT 2"), "Two transaction replays should work");

    test.log_printf("Reset the replication");
    test.repl->unblock_all_nodes();
    test.maxscale->wait_for_monitor();
    test.check_maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
    test.maxscale->wait_for_monitor();
}

void test_replay_failure(TestConnections& test)
{
    test.log_printf("Do a partial transaction");
    Connection c = test.maxscale->rwsplit();
    c.connect();
    c.query("BEGIN");
    c.query("SELECT 1");
    c.query("SELECT SLEEP(15)");

    test.log_printf("Block the node where the transaction was started");
    block_master(test);

    test.log_printf("Then block the node where the first transaction replay is attempted");
    block_master(test);

    test.log_printf("Block the final node before the replay completes");
    block_master(test);

    test.log_printf("The next query should fail as we exceeded the cap of two replays");
    test.expect(!c.query("SELECT 2"), "Three transaction replays should NOT work");

    test.log_printf("Reset the replication");
    test.repl->unblock_all_nodes();
    test.maxscale->wait_for_monitor();
    test.check_maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
    test.maxscale->wait_for_monitor();
}

void test_replay_time_limit(TestConnections& test)
{
    test.tprintf("Exceeding replay attempt limit should not matter if a time limit is configured");

    // Disable auto-failover so that we can test using only one node
    test.maxctrl("alter monitor MariaDB-Monitor auto_failover=false auto_rejoin=false");
    test.maxctrl("alter service RW-Split-Router transaction_replay_timeout=5m");

    Connection c = test.maxscale->rwsplit();
    c.connect();
    c.query("BEGIN");
    c.query("SELECT 1");
    c.query("SELECT SLEEP(15)");

    for (int i = 0; i < 3; i++)
    {
        test.repl->block_node(0);
        test.maxscale->wait_for_monitor(2);
        sleep(5);
        test.repl->unblock_node(0);
        test.maxscale->wait_for_monitor(2);
        sleep(5);
    }

    // The next query should succeed as we should be below the 5 minute time limit
    test.expect(c.query("SELECT 2"),
                "More than two transaction replays should work "
                "when transaction_replay_timeout is configured");

    test.tprintf("Exceeding replay time limit should close the connection "
                 "even if attempt limit is not reached");

    test.maxctrl("alter service RW-Split-Router "
                 "transaction_replay_timeout=15s transaction_replay_attempts=200");
    c.connect();
    c.query("BEGIN");
    c.query("SELECT 1");
    c.query("SELECT SLEEP(15)");

    for (int i = 0; i < 3; i++)
    {
        test.repl->block_node(0);
        test.maxscale->wait_for_monitor(2);
        sleep(5);
        test.repl->unblock_node(0);
        test.maxscale->wait_for_monitor(2);
        sleep(5);
    }

    // The next query should fail as we exceeded the time limit
    test.expect(!c.query("SELECT 2"), "Replay should fail when time limit is exceeded");
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("test_replay_ok");
    test_replay_ok(test);

    test.tprintf("test_replay_failure");
    test_replay_failure(test);
    test_replay_time_limit(test);

    return test.global_result;
}
