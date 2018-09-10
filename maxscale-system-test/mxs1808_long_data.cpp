#include "testconnections.h"
#include <iostream>

using namespace std;

void print_stmt_error(MYSQL_STMT *stmt, const char* msg)
{
    cout << "Error: " << msg << ": " << mysql_stmt_error(stmt) << endl;
}

static int test_long_data(MYSQL *conn, int sqlsize)
{
    int data1size = sqlsize / 2;

    char * data1 = (char*) malloc(data1size);
    memset(data1, 97, data1size);
    char * data3 = (char*) malloc(sqlsize);
    memset(data3, 99, sqlsize);

    MYSQL_STMT *stmt;
    stmt = mysql_stmt_init(conn);
    int        int_data;
    MYSQL_RES  *result;
    MYSQL_BIND my_bind[1];

    mysql_autocommit(conn, 1);

    if (NULL == stmt)
    {
        fprintf(stderr, "%s", mysql_error(conn));
        return 0;
    }
    if (mysql_stmt_prepare(stmt, "select ?", strlen("select ?")) != 0)
    {
        print_stmt_error(stmt, "stmt prepare fail");
        return 0;
    }

    memset((char*) my_bind, 0, sizeof(my_bind));

    my_bind[0].buffer = (void *)&int_data;
    my_bind[0].buffer_type = MYSQL_TYPE_STRING;

    if (mysql_stmt_bind_param(stmt, my_bind) != 0)
    {
        print_stmt_error(stmt, "bind param error");
        return 0;
    }

    /* supply data in pieces */
    if (mysql_stmt_send_long_data(stmt, 0, data1, data1size) != 0)
    {
        print_stmt_error(stmt, "send long data1 failed");
        return 0;
    }
    if (mysql_stmt_send_long_data(stmt, 0, data3, sqlsize) != 0)
    {
        print_stmt_error(stmt, "send long data3 failed");
        return 0;
    }

    /* execute */
    if (mysql_stmt_execute(stmt) != 0)
    {
        print_stmt_error(stmt, "execute prepare stmt failed");
        return 0;
    }
    /* get the result */
    result = mysql_store_result(conn);
    mysql_free_result(result);
    mysql_stmt_close(stmt);
    free(data1);
    free(data3);
    return 1;
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscales->connect();
    test.expect(test_long_data(test.maxscales->conn_rwsplit[0], 123456), "Test should work");
    test.maxscales->disconnect();

    return test.global_result;
}
