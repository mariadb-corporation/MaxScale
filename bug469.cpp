/**
 * @file bug469 bug469 regression test case ("rwsplit counts every connection twice in master - counnection counts leak")
 * - use maxadmin command "show server server1" and check "Current no. of conns" and "Number of connections" - both should be 0
 * - execute simple query against RWSplit
 * - use maxadmin command "show server server1" and check "Current no. of conns" (should be 0) and "Number of connections" (should be 1)
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    char res[1024];
    int res_d;
    Test->set_timeout(10);

    Test->get_maxadmin_param((char *) "show server server1", (char *) "Current no. of conns:", res);
    sscanf(res, "%d", &res_d);
    Test->tprintf("Before: Current num of conn %d\n", res_d);
    Test->add_result(res_d, "curr num of conn is not 0\n");
    Test->get_maxadmin_param((char *) "show server server1", (char *) "Number of connections:", res);
    sscanf(res, "%d", &res_d);
    Test->tprintf("Before: num of conn %d\n", res_d);
    Test->add_result(res_d, "num of conn is not 0");

    Test->connect_rwsplit();
    Test->try_query(Test->conn_rwsplit, (char *) "select 1");
    Test->close_rwsplit();

    Test->stop_timeout();
    sleep(10);

    Test->set_timeout(10);

    Test->get_maxadmin_param((char *) "show server server1", (char *) "Current no. of conns:", res);
    sscanf(res, "%d", &res_d);
    Test->tprintf("After: Current num of conn %d\n", res_d);
    Test->add_result(res_d, "curr num of conn is not 0\n");
    Test->get_maxadmin_param((char *) "show server server1", (char *) "Number of connections:", res);
    sscanf(res, "%d", &res_d);
    Test->tprintf("After: num of conn %d\n", res_d);
    if (res_d != 1) {Test->add_result(1, "num of conn is not 1");}

    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}
