#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int inset_select(TestConnections* Test, int N)
{
    int global_result = 0;
    create_t1(Test->conn_rwsplit);
    insert_into_t1(Test->conn_rwsplit, N);

    printf("SELECT: rwsplitter\n");
    global_result += select_from_t1(Test->conn_rwsplit, N);
    printf("SELECT: master\n");
    global_result += select_from_t1(Test->conn_master, N);
    printf("SELECT: slave\n");
    global_result += select_from_t1(Test->conn_slave, N);

    for (int i=0; i<Test->repl->N; i++) {
        printf("SELECT: directly from node %d\n", i);
        global_result += select_from_t1(Test->repl->nodes[i], N);
    }
    return(global_result);
}

int use_db(TestConnections* Test, char * db)
{
    int global_result = 0;
    char sql[100];

    sprintf(sql, "USE %s;", db);

    printf("selecting DB '%s' for rwsplit\n", db);
    global_result += execute_query(Test->conn_rwsplit, sql);
    printf("selecting DB '%s' for readconn master\n", db);
    global_result += execute_query(Test->conn_slave, sql);
    printf("selecting DB '%s' for readconn slave\n", db);
    global_result += execute_query(Test->conn_master, sql);
    for (int i = 0; i < Test->repl->N; i++) {
        printf("selecting DB '%s' for direct connection to node %d\n", db, i);
        global_result += execute_query(Test->repl->nodes[i], sql);
    }
    return(global_result);
}

int check_t1_table(TestConnections* Test, bool presence, char * db)
{
    char * expected;
    char * actual;
    int global_result = 0;
    if (presence) {
        expected = (char *) "";
        actual   = (char *) "NOT";
    } else {
        expected = (char *) "NOT";
        actual   = (char *) "";
    }

    global_result += use_db(Test, db);

    printf("Checking: table 't1' should %s be found in '%s' database\n", expected, db);
    if ( ((check_if_t1_exists(Test->conn_rwsplit) >  0) && (!presence) ) ||
         ((check_if_t1_exists(Test->conn_rwsplit) == 0) && (presence) ))
    {global_result++; printf("Table t1 is %s found in '%s' database using RWSplit\n", actual, db); } else {
        printf("RWSplit: ok\n");
    }
    if ( ((check_if_t1_exists(Test->conn_master) >  0) && (!presence) ) ||
         ((check_if_t1_exists(Test->conn_master) == 0) && (presence) ))
    {global_result++; printf("Table t1 is %s found in '%s' database using Readconnrouter with router option master\n", actual, db); } else {
        printf("ReadConn master: ok\n");
    }
    if ( ((check_if_t1_exists(Test->conn_slave) >  0) && (!presence) ) ||
         ((check_if_t1_exists(Test->conn_slave) == 0) && (presence) ))
    {global_result++; printf("Table t1 is %s found in '%s' database using Readconnrouter with router option slave\n", actual, db); } else {
        printf("ReadConn slave: ok\n");
    }
    printf("Sleeping to let replication happen\n");
    sleep(30);
    for (int i=0; i<Test->repl->N; i++) {
        if ( ((check_if_t1_exists(Test->repl->nodes[i]) >  0) && (!presence) ) ||
             ((check_if_t1_exists(Test->repl->nodes[i]) == 0) && (presence) ))
        {global_result++; printf("Table t1 is %s found in '%s' database using direct connect to node %d\n", actual, db, i); } else {
            printf("Node %d: ok\n", i);
        }
    }
    return(global_result);
}

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;
    int i;
    int N=4;

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();
    if (Test->ConnectMaxscale() !=0 ) {
        printf("Error connecting to MaxScale\n");
        exit(1);
    }

    global_result += inset_select(Test, N);

    printf("Creating database test1\n");
    global_result += execute_query(Test->conn_rwsplit, "DROP TABLE t1");
    global_result += execute_query(Test->conn_rwsplit, "DROP DATABASE IF EXISTS test1;");
    global_result += execute_query(Test->conn_rwsplit, "CREATE DATABASE test1;");
    sleep(5);

    printf("Testing with database 'test1'\n");
    global_result += use_db(Test, (char *) "test1");
    global_result += inset_select(Test, N);

    global_result += check_t1_table(Test, FALSE, (char *) "test");
    global_result += check_t1_table(Test, TRUE, (char *) "test1");



    printf("Trying queries with syntax errors\n");
    execute_query(Test->conn_rwsplit, "DROP DATABASE I EXISTS test1;");
    execute_query(Test->conn_rwsplit, "CREATE TABLE ");

    execute_query(Test->conn_master, "DROP DATABASE I EXISTS test1;");
    execute_query(Test->conn_master, "CREATE TABLE ");

    execute_query(Test->conn_slave, "DROP DATABASE I EXISTS test1;");
    execute_query(Test->conn_slave, "CREATE TABLE ");

    // close connections
    Test->CloseMaxscaleConn();
    Test->repl->CloseConn();

    global_result += CheckMaxscaleAlive();

    if (global_result == 0) {printf("PASSED!!\n");} else {printf("FAILED!!\n");}
    exit(global_result);
}
