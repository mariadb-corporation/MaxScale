#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect();
    execute_query(test.repl->nodes[0], "CREATE USER 'insert_only'@'%%' IDENTIFIED BY 'insert_only'");
    execute_query(test.repl->nodes[0], "CREATE DATABASE insert_db");
    execute_query(test.repl->nodes[0], "CREATE TABLE insert_db.t1(id INT)");
    execute_query(test.repl->nodes[0], "GRANT INSERT ON insert_db.t1 TO 'insert_only'@'%%'");
    test.repl->sync_slaves();

    MYSQL* conn = open_conn(test.maxscales->rwsplit_port[0],
                            test.maxscales->IP[0],
                            "insert_only",
                            "insert_only",
                            false);
    test.expect(mysql_errno(conn) == 0, "User without SELECT privileges should be allowed to connect");
    mysql_close(conn);

    execute_query(test.repl->nodes[0], "DROP USER 'insert_only'@'%%'");
    execute_query(test.repl->nodes[0], "DROP DATABASE insert_db");
    test.repl->sync_slaves();
    test.repl->disconnect();

    return test.global_result;
}
