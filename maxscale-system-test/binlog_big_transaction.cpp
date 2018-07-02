/**
 * @file binlog_big_transaction.cpp test of simple binlog router setup and execute a number of big transactions
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"
#include "test_binlog_fnc.h"
#include "big_transaction.h"

void *disconnect_thread( void *ptr );
TestConnections * Test ;
int exit_flag;
int main(int argc, char *argv[])
{

    Test = new TestConnections(argc, argv);
    Test->set_timeout(3000);
    Test->set_log_copy_interval(300);

    Test->repl->connect();
    execute_query(Test->repl->nodes[0], (char *) "DROP TABLE IF EXISTS t1;");
    Test->repl->close_connections();
    sleep(5);

    Test->start_binlog(0);

    pthread_t threads;
    exit_flag = 0;
    pthread_create( &threads, NULL, disconnect_thread, NULL);

    Test->repl->connect();
    for (int i = 0; i < 100000; i++)
    {
        Test->set_timeout(3000);
        Test->tprintf("Trying transactions: %d\n", i);
        Test->add_result(big_transaction(Test->repl->nodes[0], 7), "Transaction %d failed!\n", i);
    }
    Test->repl->close_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;
}

void *disconnect_thread( void *ptr )
{
    MYSQL * conn;
    char cmd[256];
    int i;
    conn = open_conn(Test->maxscales->binlog_port[0], Test->maxscales->IP[0], Test->repl->user_name,
                     Test->repl->password,
                     Test->repl->ssl);
    Test->add_result(mysql_errno(conn), "Error connecting to Binlog router, error: %s\n", mysql_error(conn));
    i = 3;
    while (exit_flag == 0)
    {
        sprintf(cmd, "DISCONNECT SERVER %d", i);
        execute_query(conn, "%s", cmd);
        i++;
        if (i > Test->repl->N)
        {
            i = 3;
            sleep(30);
            execute_query(conn, (char *) "DISCONNECT SERVER ALL");
        }
        sleep(5);
    }
    return NULL;
}
