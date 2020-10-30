/**
 * MXS-2750: Test storage of COM_STMT_EXECUTE metadata
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    MYSQL* mysql = test.maxscales->open_rwsplit_connection();

    test.try_query(mysql, "DROP TABLE IF EXISTS test.t1");
    test.try_query(mysql, "CREATE TABLE test.t1(id BIGINT)");
    test.try_query(mysql, "INSERT INTO test.t1 VALUES (1), (2), (3)");

    MYSQL_STMT *stmt = mysql_stmt_init(mysql);
    MYSQL_BIND param;

    int value = 0;
    my_bool isnull = false;
    std::string query = "SELECT id FROM test.t1 WHERE id = ?";

    memset(&param, 0, sizeof(param));
    param.buffer = &value;
    param.buffer_type = MYSQL_TYPE_LONG;
    param.is_null = &isnull;
    param.is_unsigned = false;

    test.expect(mysql_stmt_prepare(stmt, query.c_str(), query.length()) == 0,
                "Prepare failed: %s", mysql_stmt_error(stmt));

    // Wait for the prepare to reach all servers
    sleep(3);

    // Calling mysql_stmt_bind_param causes the parameter metadata to be added to the COM_STMT_EXECUTE
    mysql_stmt_bind_param(stmt, &param);

    value = 1;
    test.expect(mysql_stmt_execute(stmt) == 0, "Execute failed: %s", mysql_stmt_error(stmt));

    value = 0;
    mysql_stmt_bind_result(stmt, &param);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    mysql_stmt_free_result(stmt);

    test.expect(value == 1, "SELECT should return one row with value 1");

    test.try_query(mysql, "BEGIN");

    // Not calling mysql_stmt_bind_param will assume the metadata is the same in which case readwritesplit has to add it
    // in case the target server hasn't received it. Currently, the server will crash if it receives a COM_STMT_EXECUTE
    // for a statement which has never sent the metadata.
    value = 2;
    test.expect(mysql_stmt_execute(stmt) == 0, "Execute failed: %s", mysql_stmt_error(stmt));

    value = 0;
    mysql_stmt_bind_result(stmt, &param);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    mysql_stmt_free_result(stmt);

    test.expect(value == 2, "SELECT should return one row with value 2");

    test.try_query(mysql, "COMMIT");
    test.try_query(mysql, "DROP TABLE test.t1");
    mysql_stmt_close(stmt);
    mysql_close(mysql);

    return test.global_result;
}
