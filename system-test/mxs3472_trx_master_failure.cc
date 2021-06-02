/**
 * MXS-3472: Transaction replay is not attempted again if session commands fail
 * https://jira.mariadb.org/browse/MXS-3472
 */

#include <maxtest/testconnections.hh>

void test_master_failure(TestConnections& test)
{
    // Create a separate user so that we can easily kill the connection.
    Connection master = test.repl->get_connection(0);
    master.connect();
    master.query("CREATE USER bob IDENTIFIED BY 'bob'");
    master.query("GRANT ALL ON *.* TO bob");

    // Fill a table with some data to make sure the transaction is executed correctly.
    master.query("CREATE TABLE test.t1(id INT)");
    master.query("INSERT INTO test.t1 VALUES (1)");

    // Execute a slow session command before starting a transaction.
    test.reset_timeout();
    Connection c = test.maxscale->rwsplit();
    c.set_credentials("bob", "bob");
    c.connect();
    c.query("SET @a = (SELECT SLEEP(10))");
    c.query("BEGIN");
    c.query("UPDATE test.t1 SET id = id + 1");
    c.query("SELECT * FROM test.t1");

    // Kill the connection, wait for it to reconnect and kill it again. This should happen during the
    // execution of the session command which should trigger the code involved with the bug. If the code works
    // as expected, the transaction replay should be attempted again even if the transaction is not explicitly
    // open.
    master.query("KILL USER 'bob'");
    sleep(5);
    master.query("KILL USER 'bob'");

    // The replay should work if the session command that's done outside of a transaction fails.
    test.reset_timeout();
    test.expect(c.query("UPDATE test.t1 SET id = id + 1"), "Second update should work: %s", c.error());
    test.expect(c.query("COMMIT"), "Commit should work: %s", c.error());

    // Make sure the value is what we expect it to be. Do it inside a transaction to make sure it's routed to
    // the master.
    c.query("BEGIN");
    auto value = c.field("SELECT id FROM test.t1");
    test.expect(value == "3", "Value should be 3, it is `%s`", value.c_str());
    c.query("COMMIT");

    master.query("DROP USER bob");
    master.query("DROP TABLE test.t1");
}

void test_bad_master(TestConnections& test)
{
    Connection master = test.repl->get_connection(0);
    master.connect();
    master.query("CREATE TABLE test.t1(id INT)");

    Connection c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());

    auto check = [&](std::string query) {
            test.expect(c.query(query), "'%s' failed: %s", query.c_str(), c.error());
        };

    test.reset_timeout();
    check("SET autocommit = 0");
    check("BEGIN");
    check("INSERT INTO test.t1 VALUES (1)");

    // Stop the monitor and manually set the servers into Down state
    test.maxctrl("stop monitor MariaDB-Monitor");

    for (std::string server : {"server1", "server2", "server3", "server4"})
    {
        test.maxctrl("clear server " + server + " master");
        test.maxctrl("clear server " + server + " slave");
        test.maxctrl("clear server " + server + " running");
    }

    // Start a separate thread that starts the monitor. This causes the transaction replay to
    // finish as it will find a valid master.
    std::thread thr(
        [&]() {
            sleep(5);
            test.maxctrl("start monitor MariaDB-Monitor");
        });

    test.reset_timeout();
    check("INSERT INTO test.t1 VALUES (2)");
    check("COMMIT");
    thr.join();

    auto num_rows = c.field("SELECT COUNT(*), @@last_insert_id FROM test.t1");
    test.expect(num_rows == "2", "Table should contain two rows: %s", num_rows.c_str());

    // Disable autocommit to close the transaction and release the metadata locks on the table, otherwise the
    // DROP TABLE will hang.
    check("SET autocommit = 1");
    master.query("DROP TABLE test.t1");
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("test_master_failure");
    test_master_failure(test);

    test.tprintf("test_bad_master");
    test_bad_master(test);

    return test.global_result;
}
