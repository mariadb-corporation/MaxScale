#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.set_timeout(20);
    test.maxscales->connect();

    MYSQL_STMT* stmt = mysql_stmt_init(test.maxscales->conn_rwsplit[0]);
    std::string query = "COMMIT";
    mysql_stmt_prepare(stmt, query.c_str(), query.size());
    mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);

    stmt = mysql_stmt_init(test.maxscales->conn_rwsplit[0]);
    query = "BEGIN";
    mysql_stmt_prepare(stmt, query.c_str(), query.size());
    mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);

    test.set_timeout(30);
    execute_query_silent(test.maxscales->conn_rwsplit[0], "PREPARE test FROM 'BEGIN'");
    execute_query_silent(test.maxscales->conn_rwsplit[0], "EXECUTE test");

    test.maxscales->disconnect();

    return test.global_result;
}
