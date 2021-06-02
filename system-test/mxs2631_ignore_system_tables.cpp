/**
 * MXS-2631: Dublicate system tables found
 *
 * https://jira.mariadb.org/browse/MXS-2631
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.reset_timeout();

    MYSQL* conn = test.maxscale->open_rwsplit_connection();

    test.add_result(execute_query(conn, "SELECT 1"), "Query should succeed.");

    mysql_close(conn);
    return test.global_result;
}
