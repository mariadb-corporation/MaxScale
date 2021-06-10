/**
 * @file sysbanch_example.cpp Run 'sysbench'
 *
 * - start sysbanch test
 * - repeat for all services
 * - DROP sysbanch tables
 * - check if Maxscale is alive
 */


#include <maxtest/testconnections.hh>
#include "sysbench_commands.h"

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);

    char sys1[4096];

    Test->maxscale->ssh_node("maxscale --version-full", false);
    fflush(stdout);
    auto mxs_ip = Test->maxscale->ip4();
    Test->tprintf("Connecting to RWSplit %s\n", mxs_ip);

    sprintf(&sys1[0], SYSBENCH_PREPARE_SHORT, mxs_ip);

    Test->tprintf("Preparing sysbench tables\n%s\n", sys1);
    Test->reset_timeout();
    Test->add_result(system(sys1), "Error executing sysbench prepare\n");

    sprintf(&sys1[0], SYSBENCH_COMMAND_SHORT, mxs_ip, Test->maxscale->rwsplit_port);
    Test->set_log_copy_interval(300);
    Test->tprintf("Executing sysbench \n%s\n", sys1);
    if (system(sys1) != 0)
    {
        Test->tprintf("Error executing sysbench test\n");
    }

    Test->maxscale->connect_maxscale();

    printf("Dropping sysbanch tables!\n");
    fflush(stdout);

    /*
     *  Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "DROP TABLE sbtest1");
     *  if (!Test->smoke)
     *  {
     *   Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "DROP TABLE sbtest2");
     *   Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "DROP TABLE sbtest3");
     *   Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "DROP TABLE sbtest4");
     *  }
     */

    Test->global_result += execute_query(Test->maxscale->conn_rwsplit[0], (char*) "DROP TABLE sbtest1");

    printf("closing connections to MaxScale!\n");
    fflush(stdout);

    Test->maxscale->close_maxscale_connections();

    Test->tprintf("Checking if MaxScale is still alive!\n");
    fflush(stdout);
    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
