/**
 * @file sharding.cpp
 *
 * @verbatim

 @endverbatim
 * - check if Maxscale alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int i, j;
    char str[256];
    char user_str[256];
    char pass_str[256];

    Test->repl->stop_slaves();

    Test->restart_maxscale();

    Test->repl->connect();

    for (i = 0; i < Test->repl->N; i++) { //nodes
        for (j = 0; j < Test->repl->N; j++) { //users
            //sprintf(str, "DELETE FROM  mysql.user WHERE User='user%d';", j);
            sprintf(str, "DROP USER'user%d';", j);
            printf("%s\n", str);
            execute_query(Test->repl->nodes[i], str);

            sprintf(str, "CREATE USER 'user%d'@'%%' IDENTIFIED BY 'pass%d';", j, j);
            printf("%s\n", str);
            execute_query(Test->repl->nodes[i], str);

            sprintf(str, "DROP TABLE IF EXISTS table%d", j);
            printf("%s\n", str);
            execute_query(Test->repl->nodes[i], str);
        }
    }

    for (i = 0; i < Test->repl->N; i++) { //nodes
        printf("Node %d\t", i);
        sprintf(str, "GRANT SELECT,USAGE,CREATE ON test.* TO 'user%d'@'%%'", i);
        printf("%s\n", str);
        execute_query(Test->repl->nodes[i], str);
    }

    Test->repl->close_connections();

    MYSQL * conn[Test->repl->N];
    for (i = 0; i < Test->repl->N; i++) {
        sprintf(user_str, "user%d", i);
        sprintf(pass_str, "pass%d", i);
        printf("Open connection to Sharding router using %s %s\n", user_str, pass_str);
        conn[i] = open_conn(Test->rwsplit_port, Test->maxscale_IP, user_str, pass_str);


        sprintf(str, "CREATE TABLE table%d (x1 int, fl int);", i);
        printf("%s\n", str);
        execute_query(conn[i], str);
    }


    //global_result += check_maxscale_alive();

    Test->copy_all_logs(); return(global_result);
}
