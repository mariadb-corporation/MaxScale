/**
 * MXS-2631: Dublicate system tables found
 *
 * https://jira.mariadb.org/browse/MXS-2631
 */


#include <iostream>
#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.set_timeout(30);

    MYSQL* conn = test.maxscales->open_rwsplit_connection(0);

    test.add_result(execute_query(conn, "SELECT 1"), "Query should succeed.");

    mysql_close(conn);
    test.stop_timeout();
    sleep(1);
    test.repl->fix_replication();
    return test.global_result;
}
