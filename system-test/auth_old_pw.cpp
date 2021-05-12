/**
 * Check that old-style passwords are detected
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.repl->connect();
    execute_query(test.repl->nodes[0], "CREATE USER 'old'@'%%' IDENTIFIED BY 'old';");
    execute_query(test.repl->nodes[0], "SET PASSWORD FOR 'old'@'%%' = OLD_PASSWORD('old')");
    execute_query(test.repl->nodes[0], "FLUSH PRIVILEGES");
    test.repl->sync_slaves();

    test.set_timeout(20);
    test.tprintf("Trying to connect using user with old style password");

    MYSQL* conn = open_conn(test.maxscales->rwsplit_port,
                            test.maxscales->ip4(),
                            (char*) "old",
                            (char*)  "old",
                            test.maxscale_ssl);
    test.add_result(mysql_errno(conn) == 0, "Connections is open for the user with old style password.\n");
    mysql_close(conn);

    execute_query(test.repl->nodes[0], "DROP USER 'old'@'%%'");

    return test.global_result;
}
