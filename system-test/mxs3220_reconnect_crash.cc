/**
 * MXS-3220: Crash when session command history execution fails
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto conn = test.maxscales->rwsplit();
    conn.connect();
    conn.query("CREATE USER 'bob' IDENTIFIED BY 'bob'");
    conn.query("GRANT ALL ON *.* TO 'bob'");
    conn.disconnect();

    conn.set_credentials("bob", "bob");
    conn.connect();
    conn.query("SET @a = (SELECT SLEEP(10))");

    auto master = test.repl->get_connection(0);
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
