/**
 * @file sharding.cpp - Schema router test and regression test for MXS-78, MXS-79
 *
 * @verbatim
[MySQL Monitor]
type=monitor
module=mysqlmon
servers= server1, server2,server3  ,server4
user=skysql
passwd= skysql

[Sharding router]
type=service
router=schemarouter
servers=server1,     server2,              server3,server4
user=skysql
passwd=skysql
auth_all_servers=1
filters=QLA

 @endverbatim
 * - stop all slaves in Master/Slave setup
 * - restrt Maxscale
 * - using direct connection to backend nodes
 *    - create user0...userN users on all nodes
 *    - create sharddb on all nodes
 *    - create database 'shard_db%d" on node %d (% from 0 to N)
 *    - GRANT SELECT,USAGE,CREATE ON shard_db.* TO 'user%d'@'%%' only on node %d
 * - for every user%d
 *   - open connection to schemarouter using user%d
 * - CREATE TABLE table%d (x1 int, fl int)
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

        sprintf(str, "DROP DATABASE IF EXISTS shard_db%d", i);
        printf("%s\n", str);
        execute_query(Test->repl->nodes[i], str);

        sprintf(str, "CREATE DATABASE shard_db%d", i);
        printf("%s\n", str);
        execute_query(Test->repl->nodes[i], str);
    }

    sleep(10);
    for (i = 0; i < Test->repl->N; i++) { //nodes
        printf("Node %d\t", i);
        printf("Creating shard_db\t");
        execute_query(Test->repl->nodes[i], "CREATE DATABASE shard_db");
        sprintf(str, "GRANT SELECT,USAGE,CREATE ON shard_db.* TO 'user%d'@'%%'", i);
        printf("%s\n", str);
        global_result += execute_query(Test->repl->nodes[i], str);
    }

    Test->repl->close_connections();

    sleep(30);
    MYSQL * conn;
    for (i = 0; i < Test->repl->N; i++) {
        sprintf(user_str, "user%d", i);
        sprintf(pass_str, "pass%d", i);
        printf("Open connection to Sharding router using %s %s\n", user_str, pass_str);
        conn = open_conn_db(Test->rwsplit_port, Test->maxscale_IP, (char *) "shard_db", user_str, pass_str);

        sprintf(str, "CREATE TABLE table%d (x1 int, fl int);", i);
        printf("%s\n", str);
        global_result += execute_query(conn, str);
        mysql_close(conn);
    }

    for (i = 0; i < Test->repl->N; i++) {
        sprintf(user_str, "user%d", i);
        sprintf(pass_str, "pass%d", i);
        printf("Open connection to Sharding router using %s %s\n", user_str, pass_str);
        conn = open_conn_db(Test->rwsplit_port, Test->maxscale_IP,  (char *) "shard_db", user_str, pass_str);

        sprintf(str, "SHOW TABLES;");
        printf("%s\n", str);
        sprintf(str1, "table%d", i);
        printf("Table should be %s\n", str1);
        global_result += execute_query_check_one(conn, str, str1);
        mysql_close(conn);
    }

    Test->connect_rwsplit();

    printf("Trying USE shard_db\n");
    execute_query(Test->conn_rwsplit, "USE shard_db");

    for (i = 0; i < Test->repl->N; i++) {
        sprintf(str, "USE shard_db%d", i);
        printf("%s\n", str);
        global_result += execute_query(Test->conn_rwsplit, str);
    }

    mysql_close(Test->conn_rwsplit);

    printf("Trying to connect with empty database name\n");
    conn = open_conn_db(Test->rwsplit_port, Test->maxscale_IP, (char *) "", user_str, pass_str);
    mysql_close(conn);



    Test->copy_all_logs(); return(global_result);
}
