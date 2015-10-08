#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"


int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->repl->no_set_pos = true;

    for (int option = 0; option < 3; option++) {
        Test->set_timeout(1000);
        Test->binlog_cmd_option = option;
        Test->start_binlog();
        Test->add_result(test_binlog(Test), "binlog failed\n");
    }

    Test->copy_all_logs(); return(Test->global_result);
}
