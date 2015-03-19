/**
 * @file setup_incompl trying to start binlog setup with incomplete Maxscale.cnf
 * exectute maxadmin command to chec if there is a crash
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->print_env();
    global_result += executeMaxadminCommandPrint(Test->maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show servers");

    Test->copy_all_logs(); return(global_result);
}

