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
    char str1[256];
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

            sprintf(str, "DROP DATABASE IF EXISTS shard_db");
            printf("%s\n", str);
            execute_query(Test->repl->nodes[i], str);
        }
    }

    for (i = 0; i < Test->repl->N; i++) { //nodes
        printf("Node %d\t", i);
        execute_query(Test->repl->nodes[i], "CREATE DATABASE shard_db");
        sprintf(str, "GRANT SELECT,USAGE,CREATE ON shard_db.* TO 'user%d'@'%%'", i);
        printf("%s\n", str);
        execute_query(Test->repl->nodes[i], str);
    }

    Test->repl->close_connections();

    MYSQL * conn;
    for (i = 0; i < Test->repl->N; i++) {
        sprintf(user_str, "user%d", i);
        sprintf(pass_str, "pass%d", i);
        printf("Open connection to Sharding router using %s %s\n", user_str, pass_str);
        conn = open_conn_no_db(Test->rwsplit_port, Test->maxscale_IP, user_str, pass_str);
        execute_query(conn, "USE shard_db;");
        sprintf(str, "CREATE TABLE table%d (x1 int, fl int);", i);
        printf("%s\n", str);
        execute_query(conn, str);
        mysql_close(conn);
    }

    for (i = 0; i < Test->repl->N; i++) {
        sprintf(user_str, "user%d", i);
        sprintf(pass_str, "pass%d", i);
        printf("Open connection to Sharding router using %s %s\n", user_str, pass_str);
        conn = open_conn_no_db(Test->rwsplit_port, Test->maxscale_IP, user_str, pass_str);

        execute_query(Test->rwsplit_port, "USE shard_db");
        sprintf(str, "SHOW TABLES;");
        printf("%s\n", str);
        sprintf(str1, "table%d", i);
        printf("Table should be %s\n", str1);
        execute_query_check_one(conn, str, str1);
        mysql_close(conn);
    }

    //global_result += check_maxscale_alive();

    Test->copy_all_logs(); return(global_result);
}
