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
    for (int i=0; i<Test->repl->N; i++) {
        printf("selecting DB '%s' for direct connection to node %d\n", db, i);
        global_result += execute_query(Test->repl->nodes[i], "USE test1;");
    }
    return(global_result);
}

int check_t1_table(TestConnections* Test, bool presence)
{
    char * expected;
    char * actual;
    int global_result = 0;
    if (presence) {
        expected = (char *) "NOT";
        actual   = (char *) " ";
    } else {
        actual   = (char *) "NOT";
        expected = (char *) " ";
    }

    printf("Checking: table 't1' should %s be found in 'test' database\n", expected);
    if ( ((check_if_t1_exists(Test->conn_rwsplit) != 0) && (!presence) ) ||
         ((check_if_t1_exists(Test->conn_rwsplit) == 0) && (presence) ))
    {global_result++; printf("Table t1 is %s found in 'test' database using RWSplit\n", actual); }
    if ( ((check_if_t1_exists(Test->conn_master) != 0) && (!presence) ) ||
         ((check_if_t1_exists(Test->conn_master) == 0) && (presence) ))
    {global_result++; printf("Table t1 is %s found in 'test' database using Readconnrouter with router option master\n", actual); }
    if ( ((check_if_t1_exists(Test->conn_slave) != 0) && (!presence) ) ||
         ((check_if_t1_exists(Test->conn_slave) == 0) && (presence) ))
    {global_result++; printf("Table t1 is %s found in 'test' database using Readconnrouter with router option slave\n", actual); }
    for (int i=0; i<Test->repl->N; i++) {
        if ( ((check_if_t1_exists(Test->repl->nodes[i]) != 0) && (!presence) ) ||
             ((check_if_t1_exists(Test->repl->nodes[i]) == 0) && (presence) ))

        {global_result++; printf("Table t1 is %s found in 'test' database using direct connect to node %d\n", actual, i); }
    }
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
    Test->ConnectMaxscale();

    global_result += inset_select(Test, N);

    printf("Creating database test1\n");
    global_result += execute_query(Test->conn_rwsplit, "DROP TABLE t1");
    global_result += execute_query(Test->conn_rwsplit, "DROP DATABASE IF EXISTS test1;");
    global_result += execute_query(Test->conn_rwsplit, "CREATE DATABASE test1;");
    sleep(5);

    printf("Testing with database 'test1'\n");
    global_result += use_db(Test, (char *) "test1");
    global_result += inset_select(Test, N);

    global_result += use_db(Test, (char *) "test");
    global_result += check_t1_table(Test, FALSE);

    global_result += use_db(Test, (char *) "test1");
    global_result += check_t1_table(Test, TRUE);


    // close connections
    Test->CloseMaxscaleConn();
    Test->repl->CloseConn();

    if (global_result == 0) {printf("PASSED!!\n");} else {printf("FAILED!!\n");}
    exit(global_result);
}
