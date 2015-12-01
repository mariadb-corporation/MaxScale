/**
 * @file binlog_big_transaction.cpp test of simple binlog router setup and execute a number of big transactions
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"
#include "test_binlog_fnc.h"
#include "big_transaction.h"


int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(3000);

    Test->repl->connect();
    execute_query(Test->repl->nodes[0], (char *) "DROP TABLE IF EXISTS t1;");
    Test->repl->close_connections();
    sleep(5);

    Test->start_binlog();

    Test->repl->connect();
    for (int i = 0; i < 100000; i++)
    {
        Test->set_timeout(3000);
        Test->tprintf("Trying transactions: %d\n", i);
        Test->add_result(big_transaction(Test->repl->nodes[0], 500), "Transaction %d failed!\n", i);
    }
    Test->repl->close_connections();

    Test->copy_all_logs(); return(Test->global_result);
}
