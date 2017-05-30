/**
 * @file binlog_semisync.cpp Same test as setup_binlog, but with semisync enabled

 */

#include <my_config.h>
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
    if (Test->smoke) {options_set = 1;}

    Test->repl->connect();
    Test->tprintf("DROP TABLE t1\n");
    execute_query(Test->repl->nodes[0], (char *) "DROP TABLE IF EXISTS t1;");

    Test->tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 1;\n");
    execute_query(Test->repl->nodes[0], (char *) "SET GLOBAL rpl_semi_sync_master_enabled = 1;");
    Test->repl->close_connections();
    sleep(5);


    for (int option = 0; option < options_set; option++) {
        Test->binlog_cmd_option = option;
        Test->start_binlog();
        Test->repl->connect();
        Test->tprintf("install semisync plugin\n");
        execute_query(Test->repl->nodes[0], (char *) "INSTALL PLUGIN rpl_semi_sync_master SONAME 'semisync_master.so';");
        Test->tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 1;\n");
        execute_query(Test->repl->nodes[0], (char *) "SET GLOBAL rpl_semi_sync_master_enabled = 1;");
        Test->repl->close_connections();
        test_binlog(Test);

        Test->repl->connect();
        Test->tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 0;\n");
        execute_query(Test->repl->nodes[0], (char *) "SET GLOBAL rpl_semi_sync_master_enabled = 0;");
        Test->repl->close_connections();
        test_binlog(Test);

        Test->repl->connect();
        Test->tprintf("uninstall semisync plugin\n");
        execute_query(Test->repl->nodes[0], (char *) "UNINSTALL PLUGIN 'rpl_semi_sync_master';");
        Test->tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 1;\n");
        execute_query(Test->repl->nodes[0], (char *) "SET GLOBAL rpl_semi_sync_master_enabled = 1;");
        Test->repl->close_connections();
        test_binlog(Test);

        Test->repl->connect();
        Test->tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 0;\n");
        execute_query(Test->repl->nodes[0], (char *) "SET GLOBAL rpl_semi_sync_master_enabled = 0;");
        Test->repl->close_connections();
        test_binlog(Test);
    }

    Test->copy_all_logs(); return(Test->global_result);
}

