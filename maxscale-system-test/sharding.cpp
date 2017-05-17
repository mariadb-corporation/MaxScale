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


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(30);
    int i, j;
    char str[256];
    char str1[256];
    char user_str[256];
    char pass_str[256];

    Test->repl->stop_slaves();

    Test->restart_maxscale();

    Test->repl->connect();

    for (i = 0; i < Test->repl->N; i++)   //nodes
    {
        for (j = 0; j < Test->repl->N; j++)   //users
        {
            Test->set_timeout(30);
            execute_query(Test->repl->nodes[i], "DROP USER 'user%d'@'%%';", j);
            execute_query(Test->repl->nodes[i], "CREATE USER 'user%d'@'%%' IDENTIFIED BY 'pass%d';", j, j);
            execute_query(Test->repl->nodes[i], "DROP DATABASE IF EXISTS shard_db");
        }

        execute_query(Test->repl->nodes[i], "DROP DATABASE IF EXISTS shard_db%d", i);
        execute_query(Test->repl->nodes[i], "CREATE DATABASE shard_db%d", i);
    }
    Test->stop_timeout();

    sleep(10);
    for (i = 0; i < Test->repl->N; i++)   //nodes
    {
        Test->set_timeout(30);
        Test->tprintf("Node %d\t", i);
        Test->tprintf("Creating shard_db\t");
        execute_query(Test->repl->nodes[i], "CREATE DATABASE shard_db");
        Test->add_result(execute_query(Test->repl->nodes[i],
                                       "GRANT SELECT,USAGE,CREATE ON shard_db.* TO 'user%d'@'%%'", i), "Query should succeed.");
    }

    Test->repl->close_connections();
    Test->stop_timeout();
    sleep(30);
    MYSQL * conn;
    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(30);
        sprintf(user_str, "user%d", i);
        sprintf(pass_str, "pass%d", i);
        Test->tprintf("Open connection to Sharding router using %s %s\n", user_str, pass_str);
        conn = open_conn_db(Test->rwsplit_port, Test->maxscale_IP, (char *) "shard_db", user_str, pass_str,
                            Test->ssl);
        Test->add_result(execute_query(conn, "CREATE TABLE table%d (x1 int, fl int);", i), "Query should succeed.");
    }

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(30);
        sprintf(user_str, "user%d", i);
        sprintf(pass_str, "pass%d", i);
        Test->tprintf("Open connection to Sharding router using %s %s\n", user_str, pass_str);
        conn = open_conn_db(Test->rwsplit_port, Test->maxscale_IP,  (char *) "shard_db", user_str, pass_str,
                            Test->ssl);

        sprintf(str, "SHOW TABLES;");
        Test->tprintf("%s\n", str);
        sprintf(str1, "table%d", i);
        Test->tprintf("Table should be %s\n", str1);
        Test->add_result(execute_query_check_one(conn, str, str1), "check failed\n");
        mysql_ping(conn);
        mysql_close(conn);
    }

    Test->connect_rwsplit();

    Test->tprintf("Trying USE shard_db\n");
    execute_query(Test->conn_rwsplit, "USE shard_db");

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->add_result(execute_query(Test->conn_rwsplit, "USE shard_db%d", i), "Query should succeed.");
    }

    mysql_close(Test->conn_rwsplit);

    Test->tprintf("Trying to connect with empty database name\n");
    conn = open_conn_db(Test->rwsplit_port, Test->maxscale_IP, (char *) "", user_str, pass_str, Test->ssl);
    mysql_close(conn);

    Test->stop_timeout();
    Test->check_log_err((char *) "Length (0) is 0", false);
    Test->check_log_err((char *) "Unable to parse query", false);
    Test->check_log_err((char *) "query string allocation failed", false);

    /** Cleanup */
    for (i = 0; i < Test->repl->N; i++)
    {
        for (j = 0; j < Test->repl->N; j++)
        {
            Test->set_timeout(30);
            execute_query(Test->repl->nodes[i], "DROP USER 'user%d'@'%%';", j);
            execute_query(Test->repl->nodes[i], "DROP DATABASE IF EXISTS shard_db");
        }

        execute_query(Test->repl->nodes[i], "DROP DATABASE IF EXISTS shard_db%d", i);
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}
