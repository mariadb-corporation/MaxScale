/**
 * @file long_sysbanch.cpp Run 'sysbench' for long long execution (long load test)
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
    int port[3];
    int current_port;

    port[0] = Test->maxscales->rwsplit_port[0];
    port[1] = Test->maxscales->readconn_master_port[0];
    port[2] = Test->maxscales->readconn_slave_port[0];

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscales->IP[0]);

    sprintf(&sys1[0], SYSBENCH_PREPARE, Test->maxscales->IP[0]);

    Test->tprintf("Preparing sysbench tables\n%s\n", sys1);
    Test->set_timeout(10000);
    Test->add_result(system(sys1), "Error executing sysbench prepare\n");

    char *readonly;
    char *ro_on = (char *) "on";
    char *ro_off = (char *) "off";

    Test->stop_timeout();

    current_port = port[0];

    Test->tprintf("Trying test with port %d\n", current_port);

    if (current_port == Test->maxscales->readconn_slave_port[0] )
    {
        readonly = ro_on;
    }
    else
    {
        readonly = ro_off;
    }

    sprintf(&sys1[0], SYSBENCH_COMMAND_LONG, Test->maxscales->IP[0],
            current_port, readonly);
    Test->set_log_copy_interval(300);
    Test->tprintf("Executing sysbench \n%s\n", sys1);
    if (system(sys1) != 0)
    {
        Test->tprintf("Error executing sysbench test\n");
    }

    Test->maxscales->connect_maxscale(0);

    printf("Dropping sysbanch tables!\n");
    fflush(stdout);

    /*
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "DROP TABLE sbtest1");
    if (!Test->smoke)
    {
        Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "DROP TABLE sbtest2");
        Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "DROP TABLE sbtest3");
        Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "DROP TABLE sbtest4");
    }
    */

    Test->global_result += execute_query(Test->maxscales->conn_rwsplit[0], (char *) "DROP TABLE sbtest");

    printf("closing connections to MaxScale!\n");
    fflush(stdout);

    Test->maxscales->close_maxscale_connections(0);

    Test->tprintf("Checking if MaxScale is still alive!\n");
    fflush(stdout);
    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    fflush(stdout);
    Test->tprintf("Logs copied!\n");
    fflush(stdout);
    return rval;
}
