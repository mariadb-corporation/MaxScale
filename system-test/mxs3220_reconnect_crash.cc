/**
 * MXS-3220: Crash when session command history execution fails
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto master = test.repl->get_connection(0);
    master.connect();

    test.expect(master.connect(), "Connection failed when creating user: %s", master.error());

    for (auto row : master.rows("SELECT user, host FROM mysql.user WHERE user = 'bob'"))
    {
        test.tprintf("Dropping user '%s'@'%s'", row[0].c_str(), row[1].c_str());
        master.query("DROP USER '" + row[0] + "'@'" + row[1] + "'");
    }

    test.expect(master.query("CREATE USER 'bob' IDENTIFIED BY 'bob'"), "Query failed: %s", master.error());
    test.expect(master.query("GRANT ALL ON *.* TO 'bob'"), "Query failed: %s", master.error());
    master.disconnect();

    auto conn = test.maxscale->rwsplit();
    conn.set_credentials("bob", "bob");
    test.expect(conn.connect(), "Connection failed: %s", conn.error());
    test.expect(conn.query("SET @a = (SELECT SLEEP(10))"), "SET failed: %s", conn.error());

    master.connect();

    // Kill the current master connection. With master_failure_mode=fail_on_write this will not
    // close the connection.
    master.query("SET @id = (SELECT id FROM information_schema.processlist WHERE user = 'bob')");
    master.query("KILL @id");


    // Start a thread that kills the master connection again in five seconds. This should give enough time for
    // the reconnection and history replay to start.
    std::thread thr([&]() {
                        sleep(5);
                        master.query(
                            "SET @id = (SELECT id FROM information_schema.processlist WHERE user = 'bob')");
                        master.query("KILL @id");
                    });

    // This triggers a reconnection and the execution of the session command history
    test.expect(conn.query("SET @b = 1"), "Interrupted query should work: %s", conn.error());
    auto res = conn.field("SELECT @b");
    test.expect(!res.empty(), "User variable @b should not be empty");

    thr.join();

    conn.connect();
    conn.query("DROP USER 'bob'");

    return test.global_result;
}
