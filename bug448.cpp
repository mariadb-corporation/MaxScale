/**
 * @file bug448.cpp bug448 regression case ("Wildcard in host column of mysql.user table don't work properly")
 *
 * Test creates user1@xxx.%.%.% and tries to use it to connect
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "get_my_ip.h"

int main(int argc, char *argv[])
{
    char my_ip[1024];
    char sql[1024];
    char * first_dot;
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->read_env();
    Test->print_env();
    Test->repl->connect();
    Test->connect_maxscale();

    get_my_ip(Test->maxscale_IP, my_ip);
    printf("Test machine IP (got via network request) %s\n", my_ip);

    if (Test->get_client_ip(my_ip) != 0) {
        printf("TEST_FAILED: Unable to get IP using connection to DB");
        global_result++;
    }
    printf("Test machine IP (got via Show processlist) %s\n", my_ip);

    first_dot = strstr(my_ip, ".");
    strcpy(first_dot, ".%.%.%");

    printf("Test machine IP with %% %s\n", my_ip);


    printf("Creating user 'user1' for %s host\n", my_ip);  fflush(stdout);
    sprintf(sql, "GRANT ALL PRIVILEGES ON *.* TO user1@'%s' identified by 'pass1';  FLUSH PRIVILEGES;", my_ip);
    printf("Query: %s\n", sql); fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, sql);

    printf("Trying to open connection using user1\n");


    MYSQL * conn = open_conn(Test->rwsplit_port, Test->maxscale_IP, (char *) "user1", (char *) "pass1", Test->ssl);
    if (conn == NULL) {
        printf("TEST_FAILED! Authentification failed!\n");
        global_result++;
    } else {
        printf("Authentification for user@'%s' is ok", my_ip);
        mysql_close(conn);
    }

    sprintf(sql, "DROP USER user1@'%s';  FLUSH PRIVILEGES;", my_ip);
    global_result += execute_query(Test->conn_rwsplit, sql);

    Test->close_maxscale_connections();

    check_maxscale_alive();

    Test->copy_all_logs(); return(global_result);
}
