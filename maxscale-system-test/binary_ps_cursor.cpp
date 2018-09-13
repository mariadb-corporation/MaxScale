/**
 * Test that binary protocol cursors work as expected
 */
#include "testconnections.h"
#include <iostream>

using std::cout;
using std::endl;

void test1(TestConnections& test)
{
    test.maxscales->connect_maxscale(0);
    test.set_timeout(20);

    MYSQL_STMT* stmt = mysql_stmt_init(test.maxscales->conn_rwsplit[0]);
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

    unsigned long cursor_type = CURSOR_TYPE_READ_ONLY;
    unsigned long rows = 0;
    test.add_result(mysql_stmt_attr_set(stmt, STMT_ATTR_CURSOR_TYPE, &cursor_type),
                    "Failed to set attributes");
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
    test.maxscales->close_maxscale_connections(0);
}

void test2(TestConnections& test)
{
    test.set_timeout(20);

    MYSQL* conn = open_conn_db_timeout(test.maxscales->rwsplit_port[0],
                                       test.maxscales->ip(0),
                                       "test",
                                       test.maxscales->user_name,
                                       test.maxscales->password,
                                       1,
                                       false);

    MYSQL_STMT* stmt1 = mysql_stmt_init(conn);
    MYSQL_STMT* stmt2 = mysql_stmt_init(conn);
    const char* query1 = "SELECT @@server_id";
    const char* query2 = "SELECT @@server_id, @@last_insert_id";
    char buffer1[100] = "";
    char buffer2[100] = "";
    char buffer2_2[100] = "";
    my_bool err = false;
    my_bool isnull = false;
    MYSQL_BIND bind1[1] = {};
    MYSQL_BIND bind2[2] = {};

    bind1[0].buffer_length = sizeof(buffer1);
    bind1[0].buffer = buffer1;
    bind1[0].error = &err;
    bind1[0].is_null = &isnull;

    bind2[0].buffer_length = sizeof(buffer2);
    bind2[0].buffer = buffer2;
    bind2[0].error = &err;
    bind2[0].is_null = &isnull;
    bind2[1].buffer_length = sizeof(buffer2);
    bind2[1].buffer = buffer2_2;
    bind2[1].error = &err;
    bind2[1].is_null = &isnull;

    cout << "First prepare, should go to slave" << endl;
    test.add_result(mysql_stmt_prepare(stmt1, query1, strlen(query1)), "Failed to prepare");

    unsigned long cursor_type = CURSOR_TYPE_READ_ONLY;
    unsigned long rows = 0;
    test.add_result(mysql_stmt_attr_set(stmt1, STMT_ATTR_CURSOR_TYPE, &cursor_type),
                    "Failed to set attributes");
    test.add_result(mysql_stmt_attr_set(stmt1, STMT_ATTR_PREFETCH_ROWS, &rows), "Failed to set attributes");

    test.add_result(mysql_stmt_execute(stmt1), "Failed to execute");
    test.add_result(mysql_stmt_bind_result(stmt1, bind1), "Failed to bind result");

    int rc1 = mysql_stmt_fetch(stmt1);
    test.add_result(rc1, "Failed to fetch result: %d %s %s", rc1, mysql_stmt_error(stmt1), mysql_error(conn));
    mysql_stmt_close(stmt1);

    cout << "Second prepare, should go to master" << endl;
    test.add_result(mysql_stmt_prepare(stmt2, query2, strlen(query2)), "Failed to prepare");
    test.add_result(mysql_stmt_attr_set(stmt2, STMT_ATTR_CURSOR_TYPE, &cursor_type),
                    "Failed to set attributes");
    test.add_result(mysql_stmt_attr_set(stmt2, STMT_ATTR_PREFETCH_ROWS, &rows), "Failed to set attributes");

    test.add_result(mysql_stmt_execute(stmt2), "Failed to execute");
    test.add_result(mysql_stmt_bind_result(stmt2, bind2), "Failed to bind result");

    int rc2 = mysql_stmt_fetch(stmt2);
    test.add_result(rc2, "Failed to fetch result: %d %s %s", rc2, mysql_stmt_error(stmt2), mysql_error(conn));
    mysql_stmt_close(stmt2);

    /** Get the master's server_id */
    char server_id[1024];
    test.repl->connect();
    sprintf(server_id, "%d", test.repl->get_server_id(0));

    test.add_result(strcmp(buffer1, buffer2) == 0, "Expected results to differ");
    test.add_result(strcmp(buffer2, server_id) != 0,
                    "Expected prepare 2 to go to the master (%s) but it's %s",
                    server_id, buffer2);
}

void test3(TestConnections& test)
{
    test.maxscales->connect_maxscale(0);
    test.set_timeout(20);

    MYSQL_STMT* stmt = mysql_stmt_init(test.maxscales->conn_rwsplit[0]);
    const char* query = "SELECT @@server_id";
    char buffer[100] = "";
    my_bool err = false;
    my_bool isnull = false;
    MYSQL_BIND bind[1] = {};

    bind[0].buffer_length = sizeof(buffer);
    bind[0].buffer = buffer;
    bind[0].error = &err;
    bind[0].is_null = &isnull;

    test.add_result(mysql_stmt_prepare(stmt, query, strlen(query)), "Failed to prepare");

    cout << "Start transaction" << endl;
    test.add_result(mysql_query(test.maxscales->conn_rwsplit[0], "START TRANSACTION"),
                    "START TRANSACTION should succeed: %s",
                    mysql_error(test.maxscales->conn_rwsplit[0]));


    unsigned long cursor_type = CURSOR_TYPE_READ_ONLY;
    unsigned long rows = 0;
    test.add_result(mysql_stmt_attr_set(stmt, STMT_ATTR_CURSOR_TYPE, &cursor_type),
                    "Failed to set attributes");
    test.add_result(mysql_stmt_attr_set(stmt, STMT_ATTR_PREFETCH_ROWS, &rows), "Failed to set attributes");

    cout << "Execute" << endl;
    test.add_result(mysql_stmt_execute(stmt), "Failed to execute");
    test.add_result(mysql_stmt_bind_result(stmt, bind), "Failed to bind result");
    test.add_result(mysql_stmt_fetch(stmt), "Failed to fetch result");

    test.add_result(strlen(buffer) == 0, "Expected result buffer to not be empty");

    cout << "Commit" << endl;
    test.add_result(mysql_query(test.maxscales->conn_rwsplit[0], "COMMIT"),
                    "COMMIT should succeed: %s",
                    mysql_error(test.maxscales->conn_rwsplit[0]));

    mysql_stmt_close(stmt);
    test.maxscales->close_maxscale_connections(0);

    char server_id[1024];
    test.repl->connect();
    sprintf(server_id, "%d", test.repl->get_server_id(0));
    test.add_result(strcmp(buffer, server_id) != 0,
                    "Expected the execute inside a transaction to go to the master (%s) but it's %s",
                    server_id, buffer);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    cout << "Test 1: Testing simple cursor usage" << endl;
    test1(test);
    cout << "Done" << endl << endl;

    cout << "Test 2: Testing read-write splitting with cursors" << endl;
    test2(test);
    cout << "Done" << endl << endl;

    cout << "Test 3: Testing transactions with cursors" << endl;
    test3(test);
    cout << "Done" << endl << endl;

    return test.global_result;
}
