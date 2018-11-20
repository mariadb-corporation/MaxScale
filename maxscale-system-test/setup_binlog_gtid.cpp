/**
 * @file setup_binlog_gtid.cpp test of simple binlog router setup
 *
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"


int main(int argc, char* argv[])
{

    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(3000);

    Test->repl->connect();
    execute_query(Test->repl->nodes[0], (char*) "DROP TABLE IF EXISTS t1;");
    Test->repl->close_connections();
    sleep(5);

    Test->binlog_master_gtid = true;
    Test->binlog_slave_gtid = true;
    Test->start_binlog(0);
    test_binlog(Test);

    Test->log_excludes(0, "SET NAMES utf8mb4");
    Test->log_excludes(0, "set autocommit=1");
    Test->log_excludes(0, "select USER()");

    int rval = Test->global_result;
    delete Test;
    return rval;
}
