/**
 * MXS-3184: Route prepared statement executions with FOUND_ROWS to the previous server
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    auto conn = test.maxscales->rwsplit();
    test.expect(conn.connect(), "Connection should work: %s", conn.error());

    auto row = conn.row("SELECT SQL_CALC_FOUND_ROWS LAST_INSERT_ID(), @@server_id FROM mysql.user");
    test.expect(row.size() == 2, "SELECT should work: %s", conn.error());


    MYSQL_STMT* stmt = conn.stmt();
    const char* query = "SELECT FOUND_ROWS(), @@server_id";
    char buffer[100] = "";
    char buffer2[100] = "";
    my_bool err = false;
    my_bool isnull = false;
    my_bool err2 = false;
    my_bool isnull2 = false;
    MYSQL_BIND bind[2] = {};

    bind[0].buffer_length = sizeof(buffer);
    bind[0].buffer = buffer;
    bind[0].error = &err;
    bind[0].is_null = &isnull;
    bind[1].buffer_length = sizeof(buffer2);
    bind[1].buffer = buffer2;
    bind[1].error = &err2;
    bind[1].is_null = &isnull2;

    test.expect(mysql_stmt_prepare(stmt, query, strlen(query)) == 0, "Failed to prepare");
    test.expect(mysql_stmt_execute(stmt) == 0, "Failed to execute");
    test.expect(mysql_stmt_bind_result(stmt, bind) == 0, "Failed to bind result");
    test.expect(mysql_stmt_fetch(stmt) == 0,
                "Failed to fetch result: %s %s",
                mysql_stmt_error(stmt),
                conn.error());

    test.expect(row[1] == buffer2,
                "Expected query to be routed to server with ID %s instead of to server with ID %s",
                row[1].c_str(), buffer2);

    mysql_stmt_close(stmt);

    return test.global_result;
}
