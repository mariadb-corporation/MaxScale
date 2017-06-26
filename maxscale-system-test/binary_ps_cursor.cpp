/**
 * Test that binary protocol cursors work as expected
 */
#include "testconnections.h"
#include <iostream>

using std::cout;
using std::endl;

void test1(TestConnections& test)
{
    test.connect_maxscale();
    test.set_timeout(20);

    MYSQL_STMT* stmt = mysql_stmt_init(test.conn_rwsplit);
    const char* query = "SELECT @@server_id";
    char buffer[100] = "";
    my_bool err = false;
    my_bool isnull = false;
    MYSQL_BIND bind[1] = {};

    bind[0].buffer_length = sizeof(buffer);
    bind[0].buffer = buffer;
    bind[0].error = &err;
    bind[0].is_null = &isnull;

    cout << "Prepare" << endl;
    test.add_result(mysql_stmt_prepare(stmt, query, strlen(query)), "Failed to prepare");

    unsigned long cursor_type= CURSOR_TYPE_READ_ONLY;
    unsigned long rows=0;
    test.add_result(mysql_stmt_attr_set(stmt, STMT_ATTR_CURSOR_TYPE, &cursor_type), "Failed to set attributes");
    test.add_result(mysql_stmt_attr_set(stmt, STMT_ATTR_PREFETCH_ROWS, &rows), "Failed to set attributes");

    cout << "Execute" << endl;
    test.add_result(mysql_stmt_execute(stmt), "Failed to execute");
    cout << "Bind result" << endl;
    test.add_result(mysql_stmt_bind_result(stmt, bind), "Failed to bind result");
    cout << "Fetch row" << endl;
    test.add_result(mysql_stmt_fetch(stmt), "Failed to fetch result");

    test.add_result(strlen(buffer) == 0, "Expected result buffer to not be empty");

    cout << "Close statement" << endl;
    mysql_stmt_close(stmt);
    test.close_maxscale_connections();

}

void test2(TestConnections& test)
{
    test.set_timeout(20);

    MYSQL* conn = open_conn_db_timeout(test.rwsplit_port, test.maxscale_ip(), "test",
                                       test.maxscale_user, test.maxscale_password, 1, false);

    MYSQL_STMT* stmt1 = mysql_stmt_init(conn);
    MYSQL_STMT* stmt2 = mysql_stmt_init(conn);
    const char* query = "SELECT @@server_id";
    char buffer1[100] = "";
    char buffer2[100] = "";
    my_bool err = false;
    my_bool isnull = false;
    MYSQL_BIND bind1[1] = {};
    MYSQL_BIND bind2[1] = {};

    bind1[0].buffer_length = sizeof(buffer1);
    bind1[0].buffer = buffer1;
    bind1[0].error = &err;
    bind1[0].is_null = &isnull;

    bind2[0].buffer_length = sizeof(buffer2);
    bind2[0].buffer = buffer2;
    bind2[0].error = &err;
    bind2[0].is_null = &isnull;

    cout << "Prepare" << endl;
    test.add_result(mysql_stmt_prepare(stmt1, query, strlen(query)), "Failed to prepare");
    test.add_result(mysql_stmt_prepare(stmt2, query, strlen(query)), "Failed to prepare");

    unsigned long cursor_type= CURSOR_TYPE_READ_ONLY;
    unsigned long rows=0;
    test.add_result(mysql_stmt_attr_set(stmt1, STMT_ATTR_CURSOR_TYPE, &cursor_type), "Failed to set attributes");
    test.add_result(mysql_stmt_attr_set(stmt1, STMT_ATTR_PREFETCH_ROWS, &rows), "Failed to set attributes");
    test.add_result(mysql_stmt_attr_set(stmt2, STMT_ATTR_CURSOR_TYPE, &cursor_type), "Failed to set attributes");
    test.add_result(mysql_stmt_attr_set(stmt2, STMT_ATTR_PREFETCH_ROWS, &rows), "Failed to set attributes");

    cout << "Execute" << endl;
    test.add_result(mysql_stmt_execute(stmt1), "Failed to execute");
    test.add_result(mysql_stmt_execute(stmt2), "Failed to execute");
    cout << "Bind result" << endl;
    test.add_result(mysql_stmt_bind_result(stmt1, bind1), "Failed to bind result");
    test.add_result(mysql_stmt_bind_result(stmt2, bind2), "Failed to bind result");
    cout << "Fetch row" << endl;
    int rc1 = mysql_stmt_fetch(stmt1);
    int rc2 = mysql_stmt_fetch(stmt2);
    test.add_result(rc1, "Failed to fetch result: %d %s %s", rc1, mysql_stmt_error(stmt1), mysql_error(conn));
    test.add_result(rc2, "Failed to fetch result: %d %s %s", rc2, mysql_stmt_error(stmt2), mysql_error(conn));

    test.add_result(strlen(buffer1) == 0, "Expected result buffer 1 to not be empty");
    test.add_result(strlen(buffer2) == 0, "Expected result buffer 2 to not be empty");

    cout << "Close statement" << endl;
    mysql_stmt_close(stmt1);
    mysql_stmt_close(stmt2);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    cout << "Test 1" << endl;
    test1(test);
    cout << "Done" << endl << endl;

    cout << "Test 2" << endl;
    test2(test);
    cout << "Done" << endl << endl;

    return test.global_result;
}
