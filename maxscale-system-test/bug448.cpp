/**
 * @file bug448.cpp bug448 regression case ("Wildcard in host column of mysql.user table don't work properly")
 *
 * Test creates user1@xxx.%.%.% and tries to use it to connect
 */

#include <iostream>
#include "testconnections.h"
#include "get_my_ip.h"

int main(int argc, char *argv[])
{
    char my_ip[1024];
    char my_ip_db[1024];
    char sql[1024];
    char * first_dot;
    TestConnections * Test = new TestConnections(argc, argv);

    Test->set_timeout(20);
    Test->repl->connect();

    get_my_ip(Test->maxscale_IP, my_ip);
    Test->tprintf("Test machine IP (got via network request) %s\n", my_ip);

    Test->add_result(Test->get_client_ip(my_ip_db), "Unable to get IP using connection to DB\n");

    Test->tprintf("Test machine IP (got via Show processlist) %s\n", my_ip);

    first_dot = strstr(my_ip, ".");
    strcpy(first_dot, ".%.%.%");

    Test->tprintf("Test machine IP with %% %s\n", my_ip);

    Test->tprintf("Connecting to Maxscale\n");
    Test->add_result(Test->connect_maxscale(), "Error connecting to Maxscale\n");
    Test->tprintf("Creating user 'user1' for %s host\n", my_ip);
    Test->set_timeout(30);

    Test->add_result(execute_query(Test->conn_rwsplit, "CREATE USER user1@'%s';", my_ip),
                     "Failed to create user");
    Test->add_result(execute_query(Test->conn_rwsplit,
                                   "GRANT ALL PRIVILEGES ON *.* TO user1@'%s' identified by 'pass1';  FLUSH PRIVILEGES;", my_ip),
                     "Failed to grant privileges.");

    Test->tprintf("Trying to open connection using user1\n");

    MYSQL * conn = open_conn(Test->rwsplit_port, Test->maxscale_IP, (char *) "user1", (char *) "pass1",
                             Test->ssl);
    if (mysql_errno(conn) != 0)
    {
        Test->add_result(1, "TEST_FAILED! Authentification failed! error: %s\n", mysql_error(conn));
    }
    else
    {
        Test->tprintf("Authentification for user@'%s' is ok", my_ip);
        if (conn != NULL)
        {
            mysql_close(conn);
        }
    }

    Test->add_result(execute_query(Test->conn_rwsplit, "DROP USER user1@'%s';  FLUSH PRIVILEGES;", my_ip),
                     "Query Failed\n");

    Test->close_maxscale_connections();
    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
