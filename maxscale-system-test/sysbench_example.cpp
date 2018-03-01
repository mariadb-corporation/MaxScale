/**
 * @file sysbanch_example.cpp Run 'sysbench'
 *
 * - start sysbanch test
 * - repeat for all services
 * - DROP sysbanch tables
 * - check if Maxscale is alive
 */


#include "testconnections.h"
#include "sysbench_commands.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    char sys1[4096];

    Test->ssh_maxscale(false, "maxscale --version-full");
    fflush(stdout);
    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscale_IP);

    sprintf(&sys1[0], sysbench_prepare_short, Test->sysbench_dir, Test->sysbench_dir, Test->maxscale_IP);

    Test->tprintf("Preparing sysbench tables\n%s\n", sys1);
    Test->set_timeout(10000);
    Test->add_result(system(sys1), "Error executing sysbench prepare\n");

    Test->stop_timeout();

    sprintf(&sys1[0], sysbench_command_short, Test->sysbench_dir, Test->sysbench_dir, Test->maxscale_IP,
            Test->rwsplit_port, "off");
    Test->set_log_copy_interval(300);
    Test->tprintf("Executing sysbench \n%s\n", sys1);
    if (system(sys1) != 0)
    {
        Test->tprintf("Error executing sysbench test\n");
    }

    Test->connect_maxscale();

    printf("Dropping sysbanch tables!\n");
    fflush(stdout);

    Test->try_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest1");
    if (!Test->smoke)
    {
        Test->try_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest2");
        Test->try_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest3");
        Test->try_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest4");
    }

    //global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest");

    printf("closing connections to MaxScale!\n");
    fflush(stdout);

    Test->close_maxscale_connections();

    Test->tprintf("Checking if MaxScale is still alive!\n");
    fflush(stdout);
    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}

