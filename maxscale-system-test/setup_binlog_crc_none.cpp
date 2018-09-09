/**
 * @file setup_binlog test of simple binlog router setup
 * setup one master, one slave directly connected to real master and two slaves connected to binlog router
 * create table and put data into it using connection to master
 * check data using direct commection to all backend
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int main(int argc, char* argv[])
{

    TestConnections* Test = new TestConnections(argc, argv);

    if (!Test->smoke)
    {
        Test->binlog_cmd_option = 2;
        Test->start_binlog();

        Test->repl->connect();

        create_t1(Test->repl->nodes[0]);
        Test->add_result(insert_into_t1(Test->repl->nodes[0], 4), "error inserting data into t1\n");
        Test->tprintf("Sleeping to let replication happen\n");
        sleep(30);

        for (int i = 0; i < Test->repl->N; i++)
        {
            Test->tprintf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]);
            Test->add_result(select_from_t1(Test->repl->nodes[i], 4), "error SELECT for t1\n");
        }

        Test->repl->close_connections();
    }
    int rval = Test->global_result;
    delete Test;
    return rval;
}
