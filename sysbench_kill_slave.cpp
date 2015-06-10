/**
 * @file sysbanch_kill_slave.cpp  Kill slave during sysbanch test
 *
 * - start sysbanch test
 * - wait 20 seconds and kill active slave
 * - repeat for all services
 * - DROP sysbanch tables
 * - check if Maxscale is alive
 */


#include <my_config.h>
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
    int global_result = 0;
    pthread_t kill_vm_thread1;
    int check_iret;
    char sys1[4096];
    int port[3];

    port[0]=Test->rwsplit_port;
    port[1]=Test->readconn_master_port;
    port[2]=Test->readconn_slave_port;

    Test->read_env();
    Test->print_env();


    printf("Connecting to RWSplit %s\n", Test->maxscale_IP);
    //Test->ConnectRWSplit();
    sprintf(&sys1[0], sysbench_prepare, Test->sysbench_dir, Test->sysbench_dir, Test->maxscale_IP);
    //Test->CloseRWSplit();
    printf("Preparing sysbench tables\n%s\n", sys1);  fflush(stdout);
    if (system(sys1) != 0) {
        printf("Error executing sysbench prepare\n");
        global_result++;
    }

    char *readonly;
    char *ro_on = (char *) "on";
    char *ro_off = (char *) "off";
    for (int k = 0; k < 3; k++) {
        printf("Trying test with port %d\n", port[k]); fflush(stdout);
        check_iret = pthread_create( &kill_vm_thread1, NULL, kill_vm_thread, NULL);
        //    pthread_join(kill_vm_thread1, NULL);
        if (port[k] == Test->readconn_slave_port ) {
            readonly = ro_on;
        } else {
            readonly = ro_off;
        }
        sprintf(&sys1[0], sysbench_command, Test->sysbench_dir, Test->sysbench_dir, Test->maxscale_IP, port[k], readonly);
        printf("Executing sysbench tables\n%s\n", sys1); fflush(stdout);
        if (system(sys1) != 0) {
            printf("Error executing sysbench test\n");
            //global_result++;
        }

        printf("Starting VM back\n"); fflush(stdout);
        if ((old_slave >= 1) && (old_slave <= Test->repl->N)) {
            //sprintf(&sys1[0], "%s %s", Test->start_vm_command, Test->repl->IP[old_slave]);
            //system(sys1);fflush(stdout);
            Test->repl->unblock_node(old_slave);
        }
        sleep(60);
        printf("Restarting replication\n"); fflush(stdout);
        Test->repl->start_replication();
        sleep(30);
    }

    Test->connect_maxscale();

    printf("Dropping sysbanch tables!\n"); fflush(stdout);

    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest1");
    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest2");
    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest3");
    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest4");

    //global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest");

    printf("closing connections to MaxScale!\n"); fflush(stdout);

    Test->close_maxscale_connections();

    printf("Checxking if MaxScale is still alive!\n"); fflush(stdout);
    global_result += check_maxscale_alive();

    fflush(stdout);
    Test->copy_all_logs(); fflush(stdout);
    printf("Logs copied!\n"); fflush(stdout);
    return(global_result);
}


void *kill_vm_thread( void *ptr )
{
    //int global_result = 0;
    sleep(20);
    printf("Checking current slave\n"); fflush(stdout);
    old_slave = find_connected_slave1(Test);

    if ((old_slave >= 1) && (old_slave <= Test->repl->N)) {
        printf("Active slave is %d\n", old_slave); fflush(stdout);
    } else {
        printf("Active slave is not found, killing slave1\n"); fflush(stdout);
        old_slave = 1;
    }
    char sys1[4096];
    printf("Killing VM %s\n", Test->repl->IP[old_slave]); fflush(stdout);
    //sprintf(&sys1[0], "%s %s", Test->kill_vm_command, Test->repl->IP[old_slave]);
    //system(sys1);
    Test->repl->block_node(old_slave);
    return NULL;
}
