/**
 * @file lots_of_row.cpp INSERT extremelly big number of rows
 * - do INSERT of 100 rows in the loop 2000 times
 * - do SELECT *
 */


#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

int main(int argc, char* argv[])
{
    TestConnections::require_galera(true);
    TestConnections* Test = new TestConnections(argc, argv);
    char sql[10240];

    Test->maxscales->connect_maxscale(0);
    create_t1(Test->maxscales->conn_rwsplit[0]);

    Test->tprintf("INSERTing data");

    Test->try_query(Test->maxscales->conn_rwsplit[0], "BEGIN");
    for (int i = 0; i < 2000; i++)
    {
        Test->set_timeout(20);
        create_insert_string(sql, 100, i);
        Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", sql);
    }
    Test->try_query(Test->maxscales->conn_rwsplit[0], "COMMIT");

    Test->tprintf("done, syncing slaves");
    Test->stop_timeout();
    Test->repl->sync_slaves();
    Test->tprintf("Trying SELECT");
    Test->set_timeout(60);
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char*) "SELECT * FROM t1");

    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
