/**
 * @file rw_select_insert.cpp Checks changes of COM_SELECT and COM_INSERT after queris to check if RWSplit sends queries to master or to slave depending on if it is write or read only query
 * - connect to RWSplit, create table
 * - execute SELECT using RWSplit
 * - check COM_SELECT and COM_INSERT change on all nodes
 * - execute INSERT using RWSplit
 * - check COM_SELECT and COM_INSERT change on all nodes
 * - repeat previous steps one more time (now SELECT extracts real date, in the first case table was empty)
 * - execute SELECT 100 times, check COM_SELECT and COM_INSERT after every query (tolerate 2*N+1 queries)
 * - execute INSERT 100 times, check COM_SELECT and COM_INSERT after every query (tolerate 2*N+1 queries)
 */

#include <my_config.h>
#include "testconnections.h"
#include "get_com_select_insert.h"

int selects[256];
int inserts[256];
int new_selects[256];
int new_inserts[256];
int silent = 0;
int tolerance;

/**
 * @brief check_com_select Checks if COM_SELECT increase takes place only on one slave node and there is no COM_INSERT increase
 * @param new_selects COM_SELECT after query
 * @param new_inserts COM_INSERT after query
 * @param selects COM_SELECT before query
 * @param inserts COM_INSERT before query
 * @param Nodes pointer to Mariadb_nodes object that contains references to Master/Slave setup
 * @return 0 if COM_SELECT increased only on slave node and there is no COM_INSERT increase anywhere
 */
int check_com_select(int *new_selects, int *new_inserts, int *selects, int *inserts, Mariadb_nodes * Nodes)
{
    int i;
    int result = 0;
    int sum_selects = 0;
    int NodesNum = Nodes->N;

    if (new_selects[0]-selects[0] !=0) {result = 1; printf("SELECT query executed, but COM_INSERT increased on master\n"); }
    for (i = 0; i < NodesNum; i++) {
        if (new_inserts[i]-inserts[i] != 0) {result = 1; printf("SELECT query executed, but COM_INSERT increased\n"); }
        if (!((new_selects[i]-selects[i] == 0) || (new_selects[i]-selects[i] == 1))) {
            printf("SELECT query executed, but COM_SELECT change is %d\n", new_selects[i]-selects[i]);
            if (tolerance > 0) {
                tolerance--;
            } else {
                result=1;
            }
        }
        sum_selects += new_selects[i]-selects[i];
        selects[i] = new_selects[i]; inserts[i] = new_inserts[i];
    }
    if (sum_selects != 1) {
        printf("SELECT query executed, but COM_SELECT increased more then on one node\n");
        if ((sum_selects == 2) && (tolerance > 0)) {
            tolerance--;
        } else {
            result = 1;
        }
    }

    if (result == 0) {
        if (silent == 0) {printf("COM_SELECT increase PASS\n");}
    } else {
        printf("COM_SELECT increase FAIL\n");
    }
    return(result);
}

/**
 * @brief Checks if COM_INSERT increase takes places on all nodes and there is no COM_SELECT increase
 * @param new_selects COM_SELECT after query
 * @param new_inserts COM_INSERT after query
 * @param selects COM_SELECT before query
 * @param inserts COM_INSERT before query
 * @param Nodes pointer to Mariadb_nodes object that contains references to Master/Slave setup
 * @return 0 if COM_INSERT increases on all nodes and there is no COM_SELECT increate anywhere
 */
int check_com_insert(int *new_selects, int *new_inserts, int *selects, int *inserts, Mariadb_nodes * Nodes)
{
    int i;
    int result = 0;
    int NodesNum = Nodes->N;
    for (i = 0; i < NodesNum; i++) {
        if (new_inserts[i]-inserts[i] != 1) {
            sleep(1);
            get_global_status_allnodes(&new_selects[0], &new_inserts[0], Nodes, silent);
        }
        if (new_inserts[i]-inserts[i] != 1) {result = 1; printf("INSERT query executed, but COM_INSERT increase is %d\n", new_inserts[i]-inserts[i]); }
        if (new_selects[i]-selects[i] != 0) {
            printf("INSERT query executed, but COM_SELECT increase is %d\n", new_selects[i]-selects[i]);
            if (tolerance > 0) {
                tolerance--;
            } else {
                result=1;
            }
        }
        selects[i] = new_selects[i]; inserts[i] = new_inserts[i];
    }
    if (result == 0) {
        if (silent == 0) {printf("COM_INSERT increase PASS\n");}
    } else {
        printf("COM_INSERT increase FAIL\n");
    }
    return(result);
}


int main(int argc, char *argv[])
{
    int global_result = 0;
    int i;
    TestConnections * Test = new TestConnections(argv[0]);
    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();

    printf("Connecting to RWSplit %s\n", Test->Maxscale_IP);
    Test->ConnectRWSplit();

    tolerance=0;

    get_global_status_allnodes(&selects[0], &inserts[0], Test->repl, silent);

    printf("Creating table t1\n"); fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, "DROP TABLE IF EXISTS t1;");
    global_result += execute_query(Test->conn_rwsplit, "create table t1 (x1 int);");

    printf("Sleeping 5 seconds to let replcation happens\n"); fflush(stdout);
    sleep(5);

    printf("Trying SELECT * FROM t1\n"); fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, "select * from t1;");
    get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
    global_result += check_com_select(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl);

    printf("Trying INSERT INTO t1 VALUES(1);\n"); fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, "insert into t1 values(1);");
    get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
    global_result += check_com_insert(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl);

    printf("Trying SELECT * FROM t1\n"); fflush(stdout);
    execute_query(Test->conn_rwsplit, "select * from t1;");
    get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
    global_result += check_com_select(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl);

    printf("Trying INSERT INTO t1 VALUES(1);\n"); fflush(stdout);
    execute_query(Test->conn_rwsplit, "insert into t1 values(1);");
    get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
    global_result += check_com_insert(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl);

    int selects_before_100[255];
    int inserts_before_100[255];
    silent = 1;
    get_global_status_allnodes(&selects_before_100[0], &inserts_before_100[0], Test->repl, silent);
    printf("Doing 100 selects\n");
    tolerance=2*Test->repl->N + 1;
    for (i=0; i<100; i++) {
        global_result += execute_query(Test->conn_rwsplit, "select * from t1;");
        get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
        global_result += check_com_select(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl);
    }
    print_delta(&new_selects[0], &new_inserts[0], &selects_before_100[0], &inserts_before_100[0], Test->repl->N);

    get_global_status_allnodes(&selects_before_100[0], &inserts_before_100[0], Test->repl, silent);
    printf("Doing 100 inserts\n");
    tolerance=2*Test->repl->N + 1;
    printf("Tolerance is %d\n", tolerance);
    for (i=0; i<100; i++) {
        global_result += execute_query(Test->conn_rwsplit, "insert into t1 values(1);");
        get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
        global_result += check_com_insert(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl);
    }
    print_delta(&new_selects[0], &new_inserts[0], &selects_before_100[0], &inserts_before_100[0], Test->repl->N);

    Test->CloseRWSplit();

    Test->galera->CloseConn();
    exit(global_result);
}
