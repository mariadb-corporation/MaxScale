/**
* MXS-1760: Adding use_sql_variables_in=master resulted in error "Client requests unknown
* prepared statement ID '0' that does not map to an internal ID"
*
* https://jira.mariadb.org/browse/MXS-1760
*/

#include "testconnections.h"
#include <vector>
#include <iostream>

using namespace std;

const int NUM_STMT = 2000;

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    std::vector<MYSQL_STMT*> stmts;

    test.maxscales->connect();

    cout << "Setting variable @a to 123" << endl;
    mysql_query(test.maxscales->conn_rwsplit[0], "SET @a = 123");
    int rc = execute_query_check_one(test.maxscales->conn_rwsplit[0], "SELECT @a", "123");
    test.expect(rc == 0, "Text protocol should return 123 as the value of @a");

    cout << "Preparing and executing " << NUM_STMT << " prepared statements" << endl;
    for (int i = 0; i < NUM_STMT && test.global_result == 0; i++)
    {
        stmts.push_back(mysql_stmt_init(test.maxscales->conn_rwsplit[0]));
        MYSQL_STMT* stmt = stmts.back();
        const char* query = "SELECT @a";
        test.add_result(mysql_stmt_prepare(stmt, query, strlen(query)), "Failed to prepare: %s",
                        mysql_stmt_error(stmt));
    }

    for (auto stmt: stmts)
    {
        char buffer[100] = "";
        my_bool err = false;
        my_bool isnull = false;
        MYSQL_BIND bind[1] = {};

        bind[0].buffer_length = sizeof(buffer);
        bind[0].buffer = buffer;
        bind[0].error = &err;
        bind[0].is_null = &isnull;

        // Execute a write, should return the master's server ID

        test.add_result(mysql_stmt_execute(stmt), "Failed to execute: %s", mysql_stmt_error(stmt));
        test.add_result(mysql_stmt_bind_result(stmt, bind), "Failed to bind result: %s", mysql_stmt_error(stmt));

        while (mysql_stmt_fetch(stmt) == 0)
        {
            ;
        }

        test.add_result(strcmp(buffer, "123"), "Value is '%s', not '123'", buffer);
        mysql_stmt_close(stmt);
    }

    test.maxscales->disconnect();
    test.check_log_err(0, "unknown prepared statement", false);

    return test.global_result;
}
