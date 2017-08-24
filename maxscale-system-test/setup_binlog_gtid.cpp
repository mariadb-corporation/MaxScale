/**
 * @file setup_binlog_gtid.cpp test of simple binlog router setup

 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"


int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(3000);
    int options_set = 3;
    if (Test->smoke)
    {
        options_set = 1;
    }

    Test->repl->connect();
    execute_query(Test->repl->nodes[0], (char *) "DROP TABLE IF EXISTS t1;");
    Test->repl->close_connections();
    sleep(5);

    Test->binlog_master_gtid = true;
    Test->binlog_slave_gtid = true;
//    for (int option = 0; option < options_set; option++)
    //{
  //      Test->binlog_cmd_option = option;
        Test->start_binlog();
        test_binlog(Test);
    //}

    Test->check_log_err("SET NAMES utf8mb4", false);
    Test->check_log_err("set autocommit=1", false);
    Test->check_log_err("select USER()", false);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

