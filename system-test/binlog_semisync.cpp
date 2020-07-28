/**
 * @file binlog_semisync.cpp Same test as setup_binlog, but with semisync enabled
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"


int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.repl->connect();
    test.binlog_cmd_option = 1;
    test.start_binlog(0);
    test.repl->connect();
    test.tprintf("install semisync plugin");
    execute_query(test.repl->nodes[0],
                  (char*) "INSTALL PLUGIN rpl_semi_sync_master SONAME 'semisync_master.so';");

    test.tprintf("Reconnect");
    test.repl->close_connections();
    test.repl->connect();
    test.tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 1;");
    execute_query(test.repl->nodes[0], (char*) "SET GLOBAL rpl_semi_sync_master_enabled = 1;");
    test.repl->close_connections();
    test_binlog(&test);

    test.repl->connect();
    test.tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 0;");
    execute_query(test.repl->nodes[0], (char*) "SET GLOBAL rpl_semi_sync_master_enabled = 0;");
    test.repl->close_connections();
    test_binlog(&test);

    test.repl->connect();
    test.tprintf("uninstall semisync plugin");
    execute_query(test.repl->nodes[0], (char*) "UNINSTALL PLUGIN rpl_semi_sync_master;");
    test.tprintf("Reconnect");
    test.repl->close_connections();
    test.repl->connect();
    test.tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 1;");
    execute_query(test.repl->nodes[0], (char*) "SET GLOBAL rpl_semi_sync_master_enabled = 1;");
    test.repl->close_connections();
    test_binlog(&test);

    test.repl->connect();
    test.tprintf("SET GLOBAL rpl_semi_sync_master_enabled = 0;");
    execute_query(test.repl->nodes[0], (char*) "SET GLOBAL rpl_semi_sync_master_enabled = 0;");
    test.repl->sync_slaves();
    test.repl->close_connections();
    test_binlog(&test);

    return test.global_result;
}
