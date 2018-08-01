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


#include "testconnections.h"
#include "get_com_select_insert.h"
#include "maxadmin_operations.h"

/**
 * @brief check_com_select Checks if COM_SELECT increase takes place only on one slave node and there is no COM_INSERT increase
 * @param new_selects COM_SELECT after query
 * @param new_inserts COM_INSERT after query
 * @param selects COM_SELECT before query
 * @param inserts COM_INSERT before query
 * @param Nodes pointer to Mariadb_nodes object that contains references to Master/Slave setup
 * @return 0 if COM_SELECT increased only on slave node and there is no COM_INSERT increase anywhere
 */
int check_com_select(long int *new_selects, long int *new_inserts, long int *selects, long int *inserts,
                     Mariadb_nodes * Nodes, int expected)
{
    int i;
    int result = 0;
    int sum_selects = 0;
    int NodesNum = Nodes->N;

    if (new_selects[0] - selects[0] != 0)
    {
        result = 1;
        printf("SELECT query executed, but COM_INSERT increased on master\n");
    }

    for (i = 0; i < NodesNum; i++)
    {

        if (new_inserts[i] - inserts[i] != 0)
        {
            result = 1;
            printf("SELECT query executed, but COM_INSERT increased\n");
        }

        int diff = new_selects[i] - selects[i];
        sum_selects += diff;
        selects[i] = new_selects[i];
        inserts[i] = new_inserts[i];
    }

    if (sum_selects != expected)
    {
        printf("Expected %d SELECT queries executed, got %d\n", expected, sum_selects);
        result = 1;
    }

    if (result)
    {
        printf("COM_SELECT increase FAIL\n");
    }

    return result;
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
int check_com_insert(long int *new_selects, long int *new_inserts, long int *selects, long int *inserts,
                     Mariadb_nodes * Nodes, int expected)
{
    int result = 0;
    int diff_ins = new_inserts[0] - inserts[0];
    int diff_sel = new_selects[0] - selects[0];

    if (diff_ins == 0)
    {
        result = 1;
        printf("INSERT query executed, but COM_INSERT did not increase\n");
    }

    if (diff_sel != 0)
    {
        printf("INSERT query executed, but COM_SELECT increase is %d\n", diff_sel);
        result = 1;
    }

    selects[0] = new_selects[0];
    inserts[0] = new_inserts[0];

    if (diff_ins != expected)
    {
        printf("Expected %d INSERT queries executed, got %d\n", expected, diff_ins);
        result = 1;
    }

    if (result)
    {
        printf("COM_INSERT increase FAIL\n");
    }

    return result;
}


int main(int argc, char *argv[])
{
    long int selects[256];
    long int inserts[256];
    long int new_selects[256];
    long int new_inserts[256];

    int silent = 1;
    int i;
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(120);
    Test->repl->connect();

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscales->IP[0]);
    Test->maxscales->connect_rwsplit(0);

    Test->maxscales->execute_maxadmin_command(0, (char *) "shutdown monitor MySQL-Monitor");

    get_global_status_allnodes(&selects[0], &inserts[0], Test->repl, silent);

    Test->tprintf("Creating table t1\n");
    fflush(stdout);
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS t1;");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "create table t1 (x1 int);");

    Test->repl->sync_slaves();

    printf("Trying SELECT * FROM t1\n");
    fflush(stdout);
    get_global_status_allnodes(&selects[0], &inserts[0], Test->repl, silent);
    Test->try_query(Test->maxscales->conn_rwsplit[0], "select * from t1;");
    get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
    Test->add_result(check_com_select(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl, 1),
                     "Wrong check_com_select result\n");

    printf("Trying INSERT INTO t1 VALUES(1);\n");
    fflush(stdout);
    get_global_status_allnodes(&selects[0], &inserts[0], Test->repl, silent);
    Test->try_query(Test->maxscales->conn_rwsplit[0], "insert into t1 values(1);");
    get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
    Test->add_result(check_com_insert(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl, 1),
                     "Wrong check_com_insert result\n");

    Test->stop_timeout();
    Test->repl->sync_slaves();

    printf("Trying SELECT * FROM t1\n");
    fflush(stdout);
    get_global_status_allnodes(&selects[0], &inserts[0], Test->repl, silent);
    execute_query(Test->maxscales->conn_rwsplit[0], "select * from t1;");
    get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
    Test->add_result(check_com_select(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl, 1),
                     "Wrong check_com_select result\n");

    printf("Trying INSERT INTO t1 VALUES(1);\n");
    fflush(stdout);
    get_global_status_allnodes(&selects[0], &inserts[0], Test->repl, silent);
    execute_query(Test->maxscales->conn_rwsplit[0], "insert into t1 values(1);");
    get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
    Test->add_result(check_com_insert(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl, 1),
                     "Wrong check_com_insert result\n");

    Test->stop_timeout();
    Test->repl->sync_slaves();
    Test->tprintf("Doing 100 selects\n");

    get_global_status_allnodes(&selects[0], &inserts[0], Test->repl, silent);

    for (i = 0; i < 100 && Test->global_result == 0; i++)
    {
        Test->set_timeout(20);
        Test->try_query(Test->maxscales->conn_rwsplit[0], "select * from t1;");
    }

    Test->stop_timeout();
    Test->repl->sync_slaves();
    get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
    Test->add_result(check_com_select(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl,
                                      100),
                     "Wrong check_com_select result\n");


    Test->set_timeout(20);

    get_global_status_allnodes(&selects[0], &inserts[0], Test->repl, silent);
    Test->tprintf("Doing 100 inserts\n");

    for (i = 0; i < 100 && Test->global_result == 0; i++)
    {
        Test->set_timeout(20);
        Test->try_query(Test->maxscales->conn_rwsplit[0], "insert into t1 values(1);");
    }

    Test->stop_timeout();
    Test->repl->sync_slaves();
    get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, silent);
    Test->add_result(check_com_insert(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl,
                                      100),
                     "Wrong check_com_insert result\n");

    Test->maxscales->close_rwsplit(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
