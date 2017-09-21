/**
 * @file binlog_semisync.cpp Same test as setup_binlog, but with semisync enabled
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"


int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    Test->tprintf("Test object initialized");
    Test->set_timeout(3000);
    Test->tprintf("Trying to connect to backend");

    if (Test->repl->connect() == 0)
    {
        Test->tprintf("DROP TABLE t1");
        execute_query(Test->repl->nodes[0], (char *) "DROP TABLE IF EXISTS t1;");

        Test->repl->close_connections();
        sleep(5);

        Test->binlog_cmd_option = 1;
        Test->start_binlog();
        Test->repl->connect();
        Test->tprintf("install semisync plugin");
        execute_query(Test->repl->nodes[0],
                      (char *) "INSTALL PLUGIN rpl_semi_sync_master SONAME 'semisync_master.so';");

        Test->tprintf("Reconnect");
        Test->repl->close_connections();
        Test->repl->connect();
        Test->tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 1;");
        execute_query(Test->repl->nodes[0], (char *) "SET GLOBAL rpl_semi_sync_master_enabled = 1;");
        Test->repl->close_connections();
        test_binlog(Test);

        Test->repl->connect();
        Test->tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 0;");
        execute_query(Test->repl->nodes[0], (char *) "SET GLOBAL rpl_semi_sync_master_enabled = 0;");
        Test->repl->close_connections();
        test_binlog(Test);

        Test->repl->connect();
        Test->tprintf("uninstall semisync plugin");
        execute_query(Test->repl->nodes[0], (char *) "UNINSTALL PLUGIN rpl_semi_sync_master;");
        Test->tprintf("Reconnect");
        Test->repl->close_connections();
        Test->repl->connect();
        Test->tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 1;");
        execute_query(Test->repl->nodes[0], (char *) "SET GLOBAL rpl_semi_sync_master_enabled = 1;");
        Test->repl->close_connections();
        test_binlog(Test);

        Test->repl->connect();
        Test->tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 0;");
        execute_query(Test->repl->nodes[0], (char *) "SET GLOBAL rpl_semi_sync_master_enabled = 0;");
        sleep(10);
        Test->repl->close_connections();
        test_binlog(Test);
    }
    else
    {
        Test->add_result(1, "Can't connect to backend");
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}

