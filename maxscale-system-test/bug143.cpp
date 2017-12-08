/**
 * @file bug143.cpp bug143 regression case (MaxScale ignores host in user authentication)
 *
 * - create  user@'non_existing_host1', user@'%', user@'non_existing_host2' identified by different passwords.
 * - try to connect using RWSplit. First and third are expected to fail, second should succeed.
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->tprintf("Creating user 'user' with 3 different passwords for different hosts\n");
    Test->maxscales->connect_maxscale(0);
    execute_query(Test->maxscales->conn_rwsplit[0], "CREATE USER 'user'@'non_existing_host1' IDENTIFIED BY 'pass1'");
    execute_query(Test->maxscales->conn_rwsplit[0], "CREATE USER 'user'@'%%' IDENTIFIED BY 'pass2'");
    execute_query(Test->maxscales->conn_rwsplit[0], "CREATE USER 'user'@'non_existing_host2' IDENTIFIED BY 'pass3'");
    execute_query(Test->maxscales->conn_rwsplit[0], "GRANT ALL PRIVILEGES ON *.* TO 'user'@'non_existing_host1'");
    execute_query(Test->maxscales->conn_rwsplit[0], "GRANT ALL PRIVILEGES ON *.* TO 'user'@'%%'");
    execute_query(Test->maxscales->conn_rwsplit[0], "GRANT ALL PRIVILEGES ON *.* TO 'user'@'non_existing_host2'");

    Test->tprintf("Synchronizing slaves");
    Test->set_timeout(50);
    Test->repl->sync_slaves();

    Test->tprintf("Trying first hostname, expecting failure");
    Test->set_timeout(15);
    MYSQL * conn = open_conn(Test->maxscales->rwsplit_port[0], Test->maxscales->IP[0], (char *) "user", (char *) "pass1", Test->ssl);
    if (mysql_errno(conn) == 0)
    {
        Test->add_result(1, "MaxScale ignores host in authentication\n");
    }
    if (conn != NULL)
    {
        mysql_close(conn);
    }

    Test->tprintf("Trying second hostname, expecting success");
    Test->set_timeout(15);
    conn = open_conn(Test->maxscales->rwsplit_port[0], Test->maxscales->IP[0], (char *) "user", (char *) "pass2", Test->ssl);
    Test->add_result(mysql_errno(conn), "MaxScale can't connect: %s\n", mysql_error(conn));
    if (conn != NULL)
    {
        mysql_close(conn);
    }

    Test->tprintf("Trying third hostname, expecting failure");
    Test->set_timeout(15);
    conn = open_conn(Test->maxscales->rwsplit_port[0], Test->maxscales->IP[0], (char *) "user", (char *) "pass3", Test->ssl);
    if (mysql_errno(conn) == 0)
    {
        Test->add_result(1, "MaxScale ignores host in authentication\n");
    }
    if (conn != NULL)
    {
        mysql_close(conn);
    }

    execute_query(Test->maxscales->conn_rwsplit[0], "DROP USER 'user'@'non_existing_host1'");
    execute_query(Test->maxscales->conn_rwsplit[0], "DROP USER 'user'@'%%'");
    execute_query(Test->maxscales->conn_rwsplit[0], "DROP USER 'user'@'non_existing_host2'");
    Test->maxscales->close_maxscale_connections(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
