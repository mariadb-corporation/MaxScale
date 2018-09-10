#include "testconnections.h"
#include <iostream>
#include <functional>
#include <vector>

using namespace std;

struct Bind
{
    Bind()
    {
        bind.buffer = buffer;
        bind.buffer_type = MYSQL_TYPE_LONG;
        bind.error = &err;
        bind.is_null = &is_null;
        bind.length = &length;
    }

    MYSQL_BIND bind;
    char err = 0;
    char is_null = 0;
    char is_unsigned = 0;
    uint8_t buffer[1024];
    unsigned long length = 0;
};

struct TestCase
{
    std::string name;
    std::function<bool (MYSQL*, MYSQL_STMT*, Bind&)> func;
};

void run_test(TestConnections& test, TestCase test_case)
{
    test.maxscales->connect();

    MYSQL_STMT* stmt = mysql_stmt_init(test.maxscales->conn_rwsplit[0]);
    std::string query = "SELECT * FROM test.t1";
    unsigned long cursor_type = CURSOR_TYPE_READ_ONLY;
    mysql_stmt_attr_set(stmt, STMT_ATTR_CURSOR_TYPE, &cursor_type);

    Bind bind;

    test.set_timeout(30);

    if (mysql_stmt_prepare(stmt, query.c_str(), query.size()))
    {
        test.expect(false, "Prepared statement failure: %s", mysql_stmt_error(stmt));
    }

    cout << test_case.name << endl;
    test.expect(test_case.func(test.maxscales->conn_rwsplit[0], stmt, bind), "Test '%s' failed",
                test_case.name.c_str());

    mysql_stmt_close(stmt);

    test.expect(mysql_query(test.maxscales->conn_rwsplit[0], "SELECT 1") == 0, "Normal queries should work");

    test.maxscales->disconnect();
}


int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.maxscales->connect();

    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1(id INT)");
    test.try_query(test.maxscales->conn_rwsplit[0], "BEGIN");

    for (int i = 0; i < 100; i++)
    {
        execute_query(test.maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (%d)", i);
    }

    test.try_query(test.maxscales->conn_rwsplit[0], "COMMIT");
    test.maxscales->disconnect();

    vector<TestCase> tests =
    {
        {
            "Simple execute and fetch",
            [](MYSQL * conn, MYSQL_STMT * stmt, Bind & bind)
            {
                bool rval = true;

                if (mysql_stmt_execute(stmt) ||
                mysql_stmt_bind_result(stmt, &bind.bind))
                {
                    rval = false;
                }

                while (mysql_stmt_fetch(stmt) == 0)
                {
                    ;
                }

                return rval;
            }
        },
        {
            "Multiple overlapping executions without fetch",
            [](MYSQL * conn, MYSQL_STMT * stmt, Bind & bind)
            {
                bool rval = true;

                if (mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt))
                {
                    rval = false;
                }

                return rval;
            }
        },
        {
            "Multiple overlapping executions with fetch",
            [](MYSQL * conn, MYSQL_STMT * stmt, Bind & bind)
            {
                bool rval = true;

                if (mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_stmt_bind_result(stmt, &bind.bind))
                {
                    rval = false;
                }

                while (mysql_stmt_fetch(stmt) == 0)
                {
                    ;
                }

                return rval;
            }
        },
        {
            "Execution of queries while fetching",
            [](MYSQL * conn, MYSQL_STMT * stmt, Bind & bind)
            {
                bool rval = true;

                if (mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_stmt_bind_result(stmt, &bind.bind))
                {
                    rval = false;
                }

                while (mysql_stmt_fetch(stmt) == 0)
                {
                    mysql_query(conn, "SELECT 1");
                }

                return rval;
            }
        },
        {
            "Multiple overlapping executions and a query",
            [](MYSQL * conn, MYSQL_STMT * stmt, Bind & bind)
            {
                bool rval = true;

                if (mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_stmt_execute(stmt) ||
                mysql_query(conn, "SET @a = 1"))
                {
                    rval = false;
                }

                return rval;
            }
        }
    };

    for (auto a : tests)
    {
        run_test(test, a);
    }

    return test.global_result;
}
