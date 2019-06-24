/**
 * MXS-2521: COM_STMT_EXECUTE maybe return empty result
 * https://jira.mariadb.org/browse/MXS-2521
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.maxscales->connect();

    auto conn = test.maxscales->conn_rwsplit[0];

    test.try_query(conn, "DROP TABLE IF EXISTS double_execute;");
    test.try_query(conn, "CREATE TABLE double_execute(a int);");
    test.try_query(conn, "INSERT INTO double_execute VALUES (123), (456)");

    auto stmt = mysql_stmt_init(conn);
    std::string sql = "select a, @@server_id from double_execute where a = ?";
    test.expect(mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) == 0,
                "Prepare should work: %s", mysql_error(conn));

    int data[2] = {0, 0};
    MYSQL_BIND my_bind[2] = {};
    char is_null = 0;
    my_bind[0].buffer_type = MYSQL_TYPE_LONG;
    my_bind[0].buffer = &data[0];
    my_bind[0].buffer_length = sizeof(data[0]);
    my_bind[0].is_null = &is_null;
    my_bind[1].buffer_type = MYSQL_TYPE_LONG;
    my_bind[1].buffer = &data[1];
    my_bind[1].buffer_length = sizeof(data[2]);
    my_bind[1].is_null = &is_null;
    data[1] = 123;
    test.expect(mysql_stmt_bind_param(stmt, my_bind) == 0, "Bind: %s", mysql_stmt_error(stmt));

    // The first execute is done on the master
    test.try_query(conn, "BEGIN");

    test.expect(mysql_stmt_execute(stmt) == 0, "First execute should work: %s", mysql_stmt_error(stmt));
    data[0] = 0;

    mysql_stmt_store_result(stmt);
    test.expect(mysql_stmt_fetch(stmt) == 0, "First fetch of first execute should work");
    test.expect(data[0] == 123, "Query should return one row with value 123: `%d`", data[0]);
    test.expect(mysql_stmt_fetch(stmt) != 0, "Second fetch of first execute should NOT work");

    test.try_query(conn, "COMMIT");

    // The second execute goes to a slave, no new parameters are sent in it
    data[0] = 123;
    test.expect(mysql_stmt_execute(stmt) == 0, "Second execute should work: %s", mysql_stmt_error(stmt));
    data[0] = 0;

    mysql_stmt_store_result(stmt);

    test.expect(mysql_stmt_fetch(stmt) == 0, "First fetch of second execute should work");
    test.expect(data[0] == 123, "Query should return one row with value 123: `%d`", data[0]);
    test.expect(mysql_stmt_fetch(stmt) != 0, "Second fetch of second execute should NOT work");

    mysql_stmt_close(stmt);

    test.try_query(conn, "DROP TABLE IF EXISTS double_execute;");

    return test.global_result;
}
