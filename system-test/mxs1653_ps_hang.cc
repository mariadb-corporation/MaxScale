#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.reset_timeout();
    test.maxscale->connect();

    MYSQL_STMT* stmt = mysql_stmt_init(test.maxscale->conn_rwsplit);
    std::string query = "COMMIT";
    mysql_stmt_prepare(stmt, query.c_str(), query.size());
    mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);

    stmt = mysql_stmt_init(test.maxscale->conn_rwsplit);
    query = "BEGIN";
    mysql_stmt_prepare(stmt, query.c_str(), query.size());
    mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);

    test.reset_timeout();
    execute_query_silent(test.maxscale->conn_rwsplit, "PREPARE test FROM 'BEGIN'");
    execute_query_silent(test.maxscale->conn_rwsplit, "EXECUTE test");

    test.maxscale->disconnect();

    return test.global_result;
}
