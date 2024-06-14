/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
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

const std::string USER = "mxs2456_replay_cap";
const std::string PASSWORD = "mxs2456_replay_cap";
const std::string UPDATE = "UPDATE test.mxs2456_replay_cap SET val = val + 1 WHERE ID = 1";
const std::string KILL_USER = "KILL  CONNECTION USER " + USER + ";";
const std::string LOCK_TABLE = "LOCK TABLE test.mxs2456_replay_cap WRITE;";
const std::string UNLOCK_TABLE = "UNLOCK TABLES;";

void wait_for_reconnection(TestConnections& test, Connection& master)
{
    std::chrono::seconds time_limit{10};
    auto start = std::chrono::steady_clock::now();
    std::string count;

    do
    {
        count = master.field("SELECT COUNT(*) FROM information_schema.processlist "
                             "WHERE user = '" + USER + "'");
    }
    while (std::chrono::steady_clock::now() - start < time_limit && count == "0");

    test.expect(count != "0", "Reconnection did not take place in 10 second!");
}

void kill_and_lock(TestConnections& test, Connection& master)
{
    test.log_printf("Break the connection and block updates to the table");
    EXPECT(master.query(KILL_USER + LOCK_TABLE));
}

void kill(TestConnections& test, Connection& master)
{
    test.log_printf("Break the connection");
    EXPECT(master.query(KILL_USER));
}

void kill_and_unlock(TestConnections& test, Connection& master)
{
    test.log_printf("Break the connection and unlock the tables");
    EXPECT(master.query(KILL_USER + UNLOCK_TABLE));
}

void test_replay_ok(TestConnections& test, Connection& master)
{
    test.log_printf("Do a partial transaction");
    Connection c = test.maxscale->rwsplit();
    c.set_credentials(USER, PASSWORD);
    EXPECT(c.connect());
    EXPECT(c.query("BEGIN"));
    EXPECT(c.query("SELECT 1"));
    EXPECT(c.query(UPDATE));

    kill_and_lock(test, master);
    wait_for_reconnection(test, master);

    kill_and_unlock(test, master);
    wait_for_reconnection(test, master);

    test.log_printf("The next query should succeed as we do two replay attempts");
    test.expect(c.query("SELECT 2"), "Two transaction replays should work");
}

void test_replay_failure(TestConnections& test, Connection& master)
{
    test.log_printf("Do a partial transaction");
    Connection c = test.maxscale->rwsplit();
    c.set_credentials(USER, PASSWORD);
    EXPECT(c.connect());
    EXPECT(c.query("BEGIN"));
    EXPECT(c.query("SELECT 1"));
    EXPECT(c.query(UPDATE));

    kill_and_lock(test, master);
    wait_for_reconnection(test, master);

    kill(test, master);
    wait_for_reconnection(test, master);

    kill_and_unlock(test, master);

    test.log_printf("The next query should fail as we exceeded the cap of two replays");
    test.expect(!c.query("SELECT 2"), "Three transaction replays should NOT work");
}

void test_replay_time_limit(TestConnections& test, Connection& master)
{
    test.log_printf("Exceeding replay attempt limit should not matter if a time limit is configured");
    test.maxctrl("alter service RW-Split-Router transaction_replay_timeout=5m");

    Connection c = test.maxscale->rwsplit();
    c.set_credentials(USER, PASSWORD);
    EXPECT(c.connect());
    EXPECT(c.query("BEGIN"));
    EXPECT(c.query("SELECT 1"));
    EXPECT(c.query(UPDATE));

    kill_and_lock(test, master);
    wait_for_reconnection(test, master);

    for (int i = 0; i < 2; i++)
    {
        kill(test, master);
        wait_for_reconnection(test, master);
    }

    kill_and_unlock(test, master);
    wait_for_reconnection(test, master);

    // The next query should succeed as we should be below the 5 minute time limit
    test.expect(c.query("SELECT 2"),
                "More than two transaction replays should work "
                "when transaction_replay_timeout is configured");

    test.log_printf("Exceeding replay time limit should close the connection "
                    "even if attempt limit is not reached");

    test.maxctrl("alter service RW-Split-Router "
                 "transaction_replay_timeout=5s transaction_replay_attempts=200");

    EXPECT(c.connect());
    EXPECT(c.query("BEGIN"));
    EXPECT(c.query("SELECT 1"));
    EXPECT(c.query(UPDATE));

    kill_and_lock(test, master);
    wait_for_reconnection(test, master);

    test.log_printf("Waiting for 8 seconds");
    std::this_thread::sleep_for(std::chrono::seconds(8));

    kill_and_unlock(test, master);

    // The next query should fail as we exceeded the time limit
    test.expect(!c.query("SELECT 2"), "Replay should fail when time limit is exceeded");
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    auto c = test.repl->get_connection(0);
    EXPECT(c.connect());
    EXPECT(c.query("DROP TABLE IF EXISTS test.mxs2456_replay_cap"));
    EXPECT(c.query("CREATE TABLE test.mxs2456_replay_cap(id INT PRIMARY KEY, val INT)"));
    EXPECT(c.query("INSERT INTO test.mxs2456_replay_cap VALUES (1, 0)"));
    EXPECT(c.query("CREATE USER mxs2456_replay_cap IDENTIFIED BY 'mxs2456_replay_cap'"));
    EXPECT(c.query("GRANT ALL ON *.* TO mxs2456_replay_cap"));

    test.log_printf("test_replay_ok");
    test_replay_ok(test, c);

    test.log_printf("test_replay_failure");
    test_replay_failure(test, c);

    test.log_printf("test_replay_time_limit");
    test_replay_time_limit(test, c);

    EXPECT(c.query("DROP TABLE test.mxs2456_replay_cap"));
    EXPECT(c.query("DROP USER mxs2456_replay_cap"));
    return test.global_result;
}
