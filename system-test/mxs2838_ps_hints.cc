/**
 * MXS-2838: Hints in prepared statments
 *
 * A simple test that checks that a query that would normally be routed to a
 * slave is routed to the master when the prepared statement contains a routing
 * hint.
 */

#include <maxtest/testconnections.hh>

std::string test_one_hint(TestConnections& test, std::string hint)
{
    Connection conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection to MaxScale failed: %s", conn.error());

    MYSQL_STMT* stmt = conn.stmt();
    std::string query = "SELECT @@server_id -- maxscale " + hint;

    test.expect(mysql_stmt_prepare(stmt, query.c_str(), query.size()) == 0,
                "PREPARE failed: %s", conn.error());

    if (hint == "route to slave")
    {
        // Wait for a while to make sure the slave has completed it. The preparation of prepared statements is
        // asynchronous which means the master can accept reads if the slaves are busy.
        sleep(2);
    }

    test.expect(mysql_stmt_execute(stmt) == 0,
                "EXECUTE failed: %s", conn.error());

    char buffer[100] = "";
    my_bool err = false;
    my_bool isnull = false;
    MYSQL_BIND bind = {};

    bind.buffer_length = sizeof(buffer);
    bind.buffer = buffer;
    bind.error = &err;
    bind.is_null = &isnull;

    test.expect(mysql_stmt_bind_result(stmt, &bind) == 0,
                "Failed to bind result: %s", mysql_stmt_error(stmt));
    test.expect(mysql_stmt_fetch(stmt) == 0,
                "Failed to fetch result: %s", mysql_stmt_error(stmt));

    mysql_stmt_close(stmt);
    return buffer;
}

void test_unrelated_failure(TestConnections& test, const std::string& master_id)
{
    Connection conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection to MaxScale failed: %s", conn.error());

    MYSQL_STMT* stmt = conn.stmt();
    std::string query = "SELECT @@server_id -- maxscale route to master";

    test.expect(mysql_stmt_prepare(stmt, query.c_str(), query.size()) == 0,
                "PREPARE failed: %s", conn.error());

    // Wait for a while to make sure the prepared statement has finished on all servers.
    sleep(2);

    // Execute SQL that will result in an error
    conn.query("This will cause an error to be generated");

    test.expect(mysql_stmt_execute(stmt) == 0,
                "EXECUTE failed: %s", conn.error());

    char buffer[100] = "";
    my_bool err = false;
    my_bool isnull = false;
    MYSQL_BIND bind = {};

    bind.buffer_length = sizeof(buffer);
    bind.buffer = buffer;
    bind.error = &err;
    bind.is_null = &isnull;

    test.expect(mysql_stmt_bind_result(stmt, &bind) == 0,
                "Failed to bind result: %s", mysql_stmt_error(stmt));
    test.expect(mysql_stmt_fetch(stmt) == 0,
                "Failed to fetch result: %s", mysql_stmt_error(stmt));

    test.expect(buffer == master_id, "Expected master's ID %s, got %s.", master_id.c_str(), buffer);

    mysql_stmt_close(stmt);
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.repl->connect();
    std::string master_id = test.repl->get_server_id_str(0);
    std::string slave_id = test.repl->get_server_id_str(1);

    auto expect_eq = [&](std::string id, std::string hint) {
            auto res = test_one_hint(test, hint);
            test.expect(res == id, "Expected '%s' but got '%s' for hint: %s",
                        id.c_str(), res.c_str(), hint.c_str());
        };

    expect_eq(master_id, "route to master");
    expect_eq(master_id, "route to server server1");
    expect_eq(slave_id, "route to slave");
    expect_eq(slave_id, "route to server server2");

    // MXS-3812
    test_unrelated_failure(test, master_id);

    return test.global_result;
}
