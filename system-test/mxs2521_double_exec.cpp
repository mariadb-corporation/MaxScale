/**
 * MXS-2521: COM_STMT_EXECUTE maybe return empty result
 * https://jira.mariadb.org/browse/MXS-2521
 */

#include <maxtest/testconnections.hh>

void do_test(TestConnections& test, MYSQL* conn, bool direct)
{
    auto stmt = mysql_stmt_init(conn);
    std::string sql = "select a, @@server_id from double_execute where a = ?";
    test.expect(mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) == 0,
                "Prepare should work: %s", mysql_error(conn));

    int data_out = 123;
    MYSQL_BIND bind_out;
    char null_out = 0;
    bind_out.buffer_type = MYSQL_TYPE_LONG;
    bind_out.buffer = &data_out;
    bind_out.buffer_length = sizeof(data_out);
    bind_out.is_null = &null_out;
    test.expect(mysql_stmt_bind_param(stmt, &bind_out) == 0, "Bind: %s", mysql_stmt_error(stmt));

    // The first execute is done on the master
    test.try_query(conn, "BEGIN");

    test.expect(mysql_stmt_execute(stmt) == 0, "First execute should work: %s", mysql_stmt_error(stmt));

    int data_in[2] = {};
    MYSQL_BIND bind_in[2] = {};
    char null_in[2] = {};

    for (int i = 0; i < 2; i++)
    {
        bind_in[i].buffer_type = MYSQL_TYPE_LONG;
        bind_in[i].buffer = &data_in[i];
        bind_in[i].buffer_length = sizeof(data_in[i]);
        bind_in[i].is_null = &null_in[i];
    }

    mysql_stmt_bind_result(stmt, bind_in);
    mysql_stmt_store_result(stmt);

    test.expect(mysql_stmt_fetch(stmt) == 0, "First fetch of first execute should work");
    test.expect(data_in[0] == 123, "Query should return one row with value 123: `%d`", data_in[0]);
    test.expect(mysql_stmt_fetch(stmt) != 0, "Second fetch of first execute should NOT work");

    int first_server = data_in[1];

    test.try_query(conn, "COMMIT");

    // The second execute goes to a slave, no new parameters are sent in it
    memset(data_in, 0, sizeof(data_in));
    test.expect(mysql_stmt_execute(stmt) == 0, "Second execute should work: %s", mysql_stmt_error(stmt));

    mysql_stmt_bind_result(stmt, bind_in);
    mysql_stmt_store_result(stmt);

    test.expect(mysql_stmt_fetch(stmt) == 0, "First fetch of second execute should work");
    test.expect(data_in[0] == 123, "Query should return one row with value 123: `%d`", data_in[0]);

    if (direct)
    {
        test.expect(data_in[1] == first_server,
                    "The query should be routed to the server with server_id %d, not %d",
                    first_server, data_in[1]);
    }
    else
    {
        test.expect(data_in[1] != first_server,
                    "The query should be routed to the server with server_id %d",
                    first_server);
    }

    test.expect(mysql_stmt_fetch(stmt) != 0, "Second fetch of second execute should NOT work");

    mysql_stmt_close(stmt);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect();
    test.maxscales->connect();

    // Prepare a table
    test.try_query(test.repl->nodes[0], "DROP TABLE IF EXISTS double_execute;");
    test.try_query(test.repl->nodes[0], "CREATE TABLE double_execute(a int);");
    test.try_query(test.repl->nodes[0], "INSERT INTO double_execute VALUES (123), (456)");
    test.repl->sync_slaves();

    test.tprintf("Running test with a direct connection");
    do_test(test, test.repl->nodes[0], true);

    test.tprintf("Running test through readwritesplit");
    do_test(test, test.maxscales->conn_rwsplit[0], false);

    test.try_query(test.repl->nodes[0], "DROP TABLE IF EXISTS double_execute;");

    return test.global_result;
}
