/**
 * @file sysbanch_kill_slave.cpp  Kill slave during sysbanch test
 *
 * - start sysbanch test
 * - wait 20 seconds and kill active slave
 * - repeat for all services
 * - DROP sysbanch tables
 * - check if Maxscale is alive
 */

#include "testconnections.h"
#include "sysbench_commands.h"

TestConnections * Test ;

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;
int start_flag = 0;
int old_slave;
void *kill_vm_thread( void *ptr );

int main(int argc, char *argv[])
{
    Test = new TestConnections(argc, argv);
    pthread_t kill_vm_thread1;
    char sys1[4096];
    int port[3];

    port[0] = Test->maxscales->rwsplit_port[0];
    port[1] = Test->maxscales->readconn_master_port[0];
    port[2] = Test->maxscales->readconn_slave_port[0];

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscales->IP[0]);

    if (Test->smoke)
    {
        sprintf(&sys1[0], SYSBENCH_PREPARE1, Test->maxscales->IP[0]);
    }
    else
    {
        sprintf(&sys1[0], SYSBENCH_PREPARE, Test->maxscales->IP[0]);
    }

    Test->tprintf("Preparing sysbench tables\n%s\n", sys1);
    Test->set_timeout(5000);
    Test->add_result(system(sys1), "Error executing sysbench prepare\n");

    char *readonly;
    char *ro_on = (char *) "on";
    char *ro_off = (char *) "off";
    Test->set_timeout(2000);
    for (int k = 0; k < 3; k++)
    {
        Test->tprintf("Trying test with port %d\n", port[k]);
        pthread_create( &kill_vm_thread1, NULL, kill_vm_thread, NULL);

        if (port[k] == Test->maxscales->readconn_slave_port[0] )
        {
            readonly = ro_on;
        }
        else
        {
            readonly = ro_off;
        }
        if (Test->smoke)
        {
            sprintf(&sys1[0], SYSBENCH_COMMAND1, Test->maxscales->IP[0], port[k],
                    readonly);
        }
        else
        {
            sprintf(&sys1[0], SYSBENCH_COMMAND, Test->maxscales->IP[0], port[k],
                    readonly);
        }
        Test->tprintf("Executing sysbench tables\n%s\n", sys1);
        if (system(sys1) != 0)
        {
            Test->tprintf("Error executing sysbench test\n");
        }

        Test->tprintf("Starting VM back\n");
        if ((old_slave >= 1) && (old_slave <= Test->repl->N))
        {
            Test->repl->unblock_node(old_slave);
        }
        sleep(60);
        Test->tprintf("Restarting replication\n");
        Test->repl->start_replication();
        sleep(30);
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

    Test->tprintf("Checxking if MaxScale is still alive!\n");
    fflush(stdout);
    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}


void *kill_vm_thread( void *ptr )
{
    //int global_result = 0;
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

    printf("Killing VM %s\n", Test->repl->IP[old_slave]);
    fflush(stdout);
    Test->repl->block_node(old_slave);
    return NULL;
}
