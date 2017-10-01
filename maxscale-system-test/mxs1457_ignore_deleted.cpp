/**
 * MXS-1457: Deleted servers are not ignored when users are loaded
 *
 * Check that a corrupt and deleted server is not used to load users
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.set_timeout(60);
    test.repl->connect();
    execute_query(test.repl->nodes[0], "CREATE USER 'auth_test'@'%%' IDENTIFIED BY 'test'");
    execute_query(test.repl->nodes[0], "GRANT ALL ON *.* to 'auth_test'@'%%'");
    test.repl->sync_slaves();
    test.repl->close_connections();

    /**
     * The monitor needs to be stopped before the slaves are stopped to prevent
     * it from detecting the broken replication.
     */
    test.ssh_maxscale(true, "maxadmin shutdown monitor \"MySQL Monitor\"");
    // Stop slaves and drop the user on the master
    test.repl->stop_slaves();
    test.repl->connect();
    execute_query(test.repl->nodes[0], "DROP USER 'auth_test'@'%%'");
    test.repl->close_connections();

    test.set_timeout(60);
    MYSQL* conn = open_conn_db(test.rwsplit_port, test.maxscale_ip(), "test", "auth_test", "test", false);
    test.add_result(mysql_errno(conn) == 0, "Connection with users from master should fail");
    mysql_close(conn);

    test.ssh_maxscale(true, "maxadmin remove server server1 \"RW Split Router\"");
    conn = open_conn_db(test.rwsplit_port, test.maxscale_ip(), "test", "auth_test", "test", false);
    test.add_result(mysql_errno(conn), "Connection should be OK: %s", mysql_error(conn));
    test.try_query(conn, "SELECT 1");
    mysql_close(conn);

    test.set_timeout(60);
    test.repl->connect();
    execute_query(test.repl->nodes[1], "START SLAVE");
    execute_query(test.repl->nodes[2], "START SLAVE");
    execute_query(test.repl->nodes[3], "START SLAVE");
    test.repl->sync_slaves();
    test.repl->close_connections();

    return test.global_result;
}
