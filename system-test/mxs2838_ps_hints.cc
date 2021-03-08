/**
 * MXS-2838: Hints in prepared statments
 *
 * A simple test that checks that a query that would normally be routed to a
 * slave is routed to the master when the prepared statement contains a routing
 * hint.
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    Connection conn = test.maxscales->rwsplit();
    Connection master = test.repl->get_connection(0);

    test.expect(master.connect(), "Connection to master failed: %s", master.error());
    test.expect(conn.connect(), "Connection to MaxScale failed: %s", conn.error());

    std::string id = master.field("SELECT @@server_id");
    MYSQL_STMT* stmt = conn.stmt();

    std::string query = "SELECT @@server_id -- maxscale route to master";

    test.expect(mysql_stmt_prepare(stmt, query.c_str(), query.size()) == 0,
                "PREPARE failed: %s", conn.error());
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

    test.expect(buffer == id, "Expected ID '%s' but got ID '%s'", id.c_str(), buffer);

    mysql_stmt_close(stmt);

    return test.global_result;
}
