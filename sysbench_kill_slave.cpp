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
    Test = new TestConnections(argv[0]);
    int global_result = 0;
    pthread_t kill_vm_thread1;
    int check_iret;
    char sys1[4096];
    int port[3];

    port[0]=Test->rwsplit_port;
    port[1]=Test->readconn_master_port;
    port[2]=Test->readconn_slave_port;

    Test->ReadEnv();
    Test->PrintIP();


    printf("Connecting to RWSplit %s\n", Test->Maxscale_IP);
    //Test->ConnectRWSplit();
    sprintf(&sys1[0], sysbench_prepare, Test->SysbenchDir, Test->SysbenchDir, Test->Maxscale_IP);
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
        sprintf(&sys1[0], sysbench_command, Test->SysbenchDir, Test->SysbenchDir, Test->Maxscale_IP, port[k], readonly);
        printf("Executing sysbench tables\n%s\n", sys1); fflush(stdout);
        if (system(sys1) != 0) {
            printf("Error executing sysbench test\n");
            //global_result++;
        }

        printf("Starting VM back\n"); fflush(stdout);
        if ((old_slave >= 1) && (old_slave <= Test->repl->N)) {
            sprintf(&sys1[0], "%s %s", Test->StartVMCommand, Test->repl->IP[old_slave]);
            system(sys1);fflush(stdout);
        }
        sleep(60);
        printf("Restarting replication\n"); fflush(stdout);
        Test->repl->StartReplication();
        sleep(30);
    }

    Test->ConnectMaxscale();

    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest1");
    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest2");
    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest3");
    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest4");

    //global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest");

    Test->CloseMaxscaleConn();
    global_result += CheckMaxscaleAlive();



    Test->Copy_all_logs(); return(global_result);
}


void *kill_vm_thread( void *ptr )
{
    //int global_result = 0;
    sleep(20);
    printf("Checking current slave\n"); fflush(stdout);
    old_slave = FindConnectedSlave1(Test);

    if ((old_slave >= 1) && (old_slave <= Test->repl->N)) {
        printf("Active slave is %d\n", old_slave); fflush(stdout);
    } else {
        printf("Active slave is not found, killing slave1\n"); fflush(stdout);
        old_slave = 1;
    }
    char sys1[4096];
    printf("Killing VM %s\n", Test->repl->IP[old_slave]); fflush(stdout);
    sprintf(&sys1[0], "%s %s", Test->KillVMCommand, Test->repl->IP[old_slave]);
    system(sys1);
    return NULL;
}
