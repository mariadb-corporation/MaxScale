/**
 * Test for MXS-1295: https://jira.mariadb.org/browse/MXS-1295
 */
#include "testconnections.h"

const char sp_sql[] =
    "DROP PROCEDURE IF EXISTS multi;"
    "CREATE PROCEDURE multi()"
    "BEGIN"
    "    SELECT @@server_id;"
    "END";

int get_server_id(MYSQL* conn)
{
    char value[200] = "";
    find_field(conn, "SELECT @@server_id", "@@server_id", value);
    return atoi(value);
}

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.maxscales->connect_maxscale(0);
    test.repl->connect();

    test.tprintf("Create the stored procedure and check that it works");
    test.try_query(test.repl->nodes[0], sp_sql);
    test.try_query(test.repl->nodes[0], "CALL multi()");

    test.tprintf("Check that queries after a CALL command get routed to the master");

    int master = get_server_id(test.repl->nodes[0]);
    int slave = get_server_id(test.repl->nodes[1]);
    int result = get_server_id(test.maxscales->conn_rwsplit[0]);

    test.add_result(result != slave, "The query should be routed to a slave(%d): %d", slave, result);
    test.try_query(test.maxscales->conn_rwsplit[0], "USE test");
    test.try_query(test.maxscales->conn_rwsplit[0], "CALL multi()");
    result = get_server_id(test.maxscales->conn_rwsplit[0]);
    test.add_result(result != master, "The query should be routed to the master(%d): %d", master, result);

    return test.global_result;
}
