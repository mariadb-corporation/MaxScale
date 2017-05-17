/**
 * @file avro_long.cpp test of avro
 * - setup binlog and avro
 * - put some data to t1 in the loop
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(600);
    Test->stop_maxscale();
    Test->ssh_maxscale(true, (char *) "rm -rf /var/lib/maxscale/avro");

    //Test->ssh_maxscale(true, (char *) "mkdir /var/lib/maxscale/avro; chown -R maxscale:maxscale /var/lib/maxscale/avro");

    Test->repl->connect();
    execute_query(Test->repl->nodes[0], (char *) "DROP TABLE IF EXISTS t1;");
    Test->repl->close_connections();
    sleep(5);


    Test->start_binlog();

    Test->set_timeout(120);

    Test->stop_maxscale();

    Test->ssh_maxscale(true, (char *) "rm -rf /var/lib/maxscale/avro");

    Test->set_timeout(120);

    Test->start_maxscale();

    Test->set_timeout(60);

    Test->repl->connect();
    create_t1(Test->repl->nodes[0]);

    for (int i = 0; i < 1000000; i++)
    {
        Test->set_timeout(60);
        insert_into_t1(Test->repl->nodes[0], 3);
        Test->tprintf("i=%d\n", i);
    }

    Test->repl->close_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
