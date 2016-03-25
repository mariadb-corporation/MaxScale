/**
 * @file
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    char sql[1024];

    Test->connect_maxscale();
    create_t1(Test->conn_rwsplit);

    Test->tprintf("INSERTing data\n");
    for (int i = 0; i < 200000; i++)
    {
        Test->set_timeout(10);
        sprintf(sql, "INSERT INTO t1 (x1, fl) VALUES (%d, 1)", i);
        Test->try_query(Test->conn_rwsplit, sql);
    }
    Test->tprintf("done, sleeping\n");
    Test->stop_timeout();
    sleep(20);
    Test->tprintf("Trying SELECT\n");
    Test->set_timeout(10);
    Test->try_query(Test->conn_rwsplit, (char *) "SELECT * FROM t1");

    Test->check_maxscale_alive();
    Test->copy_all_logs(); return(Test->global_result);
}

