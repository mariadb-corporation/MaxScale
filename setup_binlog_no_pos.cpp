#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"


int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    MYSQL * binlog;
    int i;

    Test->read_env();
    Test->print_env();

    Test->repl->no_set_pos = true;

    for (int option = 0; option < 3; option++) {
        Test->binlog_cmd_option = option;
        Test->start_binlog();
        global_result += test_binlog(Test, binlog);
    }

    Test->copy_all_logs(); return(global_result);
}
