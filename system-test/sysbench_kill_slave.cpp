/**
 * @file sysbanch_kill_slave.cpp  Kill slave during sysbanch test
 *
 * - start sysbanch test
 * - wait 20 seconds and kill active slave
 * - repeat for all services
 * - DROP sysbanch tables
 * - check if Maxscale is alive
 */

#include <maxtest/testconnections.hh>
#include "sysbench_commands.h"

TestConnections* Test;

int exit_flag = 0;
int old_slave;
void* kill_vm_thread();

int main(int argc, char* argv[])
{
    Test = new TestConnections(argc, argv);
    char sys1[4096];
    int port[3];

    port[0] = Test->maxscales->rwsplit_port;
    port[1] = Test->maxscales->readconn_master_port[0];

    auto mxs_ip = Test->maxscales->ip4();
    Test->tprintf("Connecting to RWSplit %s\n", mxs_ip);

    if (Test->smoke)
    {
        sprintf(&sys1[0], SYSBENCH_PREPARE1, mxs_ip);
    }
    else
    {
        sprintf(&sys1[0], SYSBENCH_PREPARE, mxs_ip);
    }

    Test->tprintf("Preparing sysbench tables\n%s\n", sys1);
    Test->set_timeout(5000);
    Test->add_result(system(sys1), "Error executing sysbench prepare\n");

    Test->set_timeout(2000);
    for (int k = 0; k < 2; k++)
    {
        Test->tprintf("Trying test with port %d\n", port[k]);
        std::thread kill_vm_thread1(kill_vm_thread);

        if (Test->smoke)
        {
            sprintf(&sys1[0], SYSBENCH_COMMAND1, mxs_ip, port[k]);
        }
        else
        {
            sprintf(&sys1[0], SYSBENCH_COMMAND, mxs_ip, port[k]);
        }
        Test->tprintf("Executing sysbench tables\n%s\n", sys1);
        if (system(sys1) != 0)
        {
            Test->tprintf("Error executing sysbench test\n");
        }

        kill_vm_thread1.join();     // Correct place to join?

        Test->tprintf("Starting VM back\n");
        if ((old_slave >= 1) && (old_slave <= Test->repl->N))
        {
            Test->repl->unblock_node(old_slave);
        }
    }

    Test->maxscales->connect_maxscale();

    printf("Dropping sysbanch tables!\n");
    fflush(stdout);

    Test->global_result += execute_query(Test->maxscales->conn_rwsplit[0], "DROP TABLE sbtest1");

    printf("closing connections to MaxScale!\n");
    fflush(stdout);

    Test->maxscales->close_maxscale_connections();

    Test->tprintf("Checking if MaxScale is still alive!");
    fflush(stdout);
    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}


void* kill_vm_thread()
{
    sleep(20);
    printf("Checking current slave\n");
    fflush(stdout);
    old_slave = Test->find_connected_slave1(0);

    if ((old_slave >= 1) && (old_slave <= Test->repl->N))
    {
        printf("Active slave is %d\n", old_slave);
        fflush(stdout);
    }
    else
    {
        printf("Active slave is not found, killing slave1\n");
        fflush(stdout);
        old_slave = 1;
    }

    printf("Killing VM %s\n", Test->repl->ip4(old_slave));
    fflush(stdout);
    Test->repl->block_node(old_slave);
    return NULL;
}
