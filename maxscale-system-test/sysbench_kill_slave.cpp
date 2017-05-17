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
unsigned int old_slave;
void *kill_vm_thread( void *ptr );

int main(int argc, char *argv[])
{
    Test = new TestConnections(argc, argv);
    pthread_t kill_vm_thread1;
    int check_iret;
    char sys1[4096];
    int port[3];

    port[0] = Test->rwsplit_port;
    port[1] = Test->readconn_master_port;
    port[2] = Test->readconn_slave_port;

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscale_IP);

    if (Test->smoke)
    {
        sprintf(&sys1[0], sysbench_prepare1, Test->sysbench_dir, Test->sysbench_dir, Test->maxscale_IP);
    }
    else
    {
        sprintf(&sys1[0], sysbench_prepare, Test->sysbench_dir, Test->sysbench_dir, Test->maxscale_IP);
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
        check_iret = pthread_create( &kill_vm_thread1, NULL, kill_vm_thread, NULL);

        if (port[k] == Test->readconn_slave_port )
        {
            readonly = ro_on;
        }
        else
        {
            readonly = ro_off;
        }
        if (Test->smoke)
        {
            sprintf(&sys1[0], sysbench_command1, Test->sysbench_dir, Test->sysbench_dir, Test->maxscale_IP, port[k],
                    readonly);
        }
        else
        {
            sprintf(&sys1[0], sysbench_command, Test->sysbench_dir, Test->sysbench_dir, Test->maxscale_IP, port[k],
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

    printf("closing connections to MaxScale!\n");
    fflush(stdout);

    Test->close_maxscale_connections();

    Test->tprintf("Checxking if MaxScale is still alive!\n");
    fflush(stdout);
    Test->check_maxscale_alive();

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
    old_slave = Test->find_connected_slave1();

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
    char sys1[4096];
    printf("Killing VM %s\n", Test->repl->IP[old_slave]);
    fflush(stdout);
    Test->repl->block_node(old_slave);
    return NULL;
}
