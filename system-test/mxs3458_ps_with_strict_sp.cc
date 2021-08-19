#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    MYSQL* mysql = test.maxscale->open_rwsplit_connection();

    test.expect(mysql_query(mysql,
                            "CREATE OR REPLACE PROCEDURE MY_TEST_SP(IN id INT)"
                            " LANGUAGE SQL DETERMINISTIC READS SQL DATA"
                            " BEGIN"
                            " SELECT id;"
                            " END") == 0,
                "Failed to create procedure: %s", mysql_error(mysql));

    test.expect(mysql_query(mysql, "CALL MY_TEST_SP(321)") == 0,
                "Failed to call stored procedure: %s", mysql_error(mysql));

    do
    {
        mysql_free_result(mysql_use_result(mysql));
    }
    while(mysql_next_result(mysql));

    MYSQL_STMT* stmt = mysql_stmt_init(mysql);

    uint64_t buffer = 123;
    char err;
    char isnull = false;
    MYSQL_BIND param;
    param.buffer = &buffer;
    param.buffer_type = MYSQL_TYPE_LONG;
    param.is_null = &isnull;
    param.is_unsigned = false;
    param.error = &err;

    uint64_t buffer2 = 0;
    MYSQL_BIND param_res;
    param_res.buffer = &buffer2;
    param_res.is_null = &isnull;
    param_res.error = &err;
    param_res.buffer_type = MYSQL_TYPE_LONG;

    std::string query = "SELECT ?";

    if (mysql_stmt_prepare(stmt, query.c_str(), query.length())
        || mysql_stmt_bind_param(stmt, &param)
        || mysql_stmt_execute(stmt)
        || mysql_stmt_bind_result(stmt, &param_res))
    {
        test.add_failure("Prepared statement failed: %s", mysql_stmt_error(stmt));
    }

    if (mysql_stmt_fetch(stmt) == 0)
    {
        test.expect(buffer2 == 123, "Prepared statement returned %lu when %lu was expected", buffer2, buffer);
    }

    mysql_stmt_close(stmt);
    mysql_close(mysql);

    return test.global_result;
}
