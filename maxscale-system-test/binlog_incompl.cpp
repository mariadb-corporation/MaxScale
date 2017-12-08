/**
 * @file setup_incompl trying to start binlog setup with incomplete Maxscale.cnf
 * check for crash
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->set_timeout(60);
    Test->maxscales->connect_maxscale(0);
    Test->maxscales->close_maxscale_connections(0);
    sleep(10);
    Test->check_log_err(0, "fatal signal 11", false);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

