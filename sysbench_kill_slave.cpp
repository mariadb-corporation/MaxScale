#include <my_config.h>
#include "testconnections.h"
#include "sysbench_commands.h"

TestConnections * Test ;

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;
int start_flag = 0;
unsigned int old_slave;
void *kill_vm_thread( void *ptr );

int main()
{
    Test = new TestConnections();
    int global_result = 0;
    pthread_t kill_vm_thread1;
    int check_iret;
    char sys1[4096];
    int port[3];

    port[0]=4006;
    port[1]=4008;
    port[2]=4009;

    Test->ReadEnv();
    Test->PrintIP();


    printf("Connecting to RWSplit %s\n", Test->Maxscale_IP);
    //Test->ConnectRWSplit();
    sprintf(&sys1[0], sysbench_prepare, Test->Maxscale_IP);
    //Test->CloseRWSplit();
    printf("Preparing sysbench tables\n%s\n", sys1);  fflush(stdout);
    if (system(sys1) != 0) {
        printf("Error executing sysbench prepare\n");
        global_result++;
    }

    for (int k = 0; k < 3; k++) {
        printf("Trying test with port %d\n", port[k]); fflush(stdout);
        check_iret = pthread_create( &kill_vm_thread1, NULL, kill_vm_thread, NULL);
        //    pthread_join(kill_vm_thread1, NULL);
        sprintf(&sys1[0], sysbench_command, Test->Maxscale_IP, port[k]);
        printf("Executing sysbench tables\n%s\n", sys1); fflush(stdout);
        if (system(sys1) != 0) {
            printf("Error executing sysbench test\n");
            global_result++;
        }

        printf("Starting VM back\n"); fflush(stdout);
        sprintf(&sys1[0], "%s %s", Test->StartVMCommand, Test->repl->IP[old_slave]);
        system(sys1);fflush(stdout);

        sleep(60);
    }

    //global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest1");
    //global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest2");
    //global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest3");
    //global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest4");

    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest");

    exit(global_result);
}


void *kill_vm_thread( void *ptr )
{
    int global_result = 0;
    sleep(20);
    printf("Checking current slave\n"); fflush(stdout);
    old_slave = FindConnectedSlave1(Test, &global_result, 32);

    char sys1[4096];
    printf("Killing VM %s\n", Test->repl->IP[old_slave]); fflush(stdout);
    sprintf(&sys1[0], "%s %s", Test->KillVMCommand, Test->repl->IP[old_slave]);
    system(sys1);
    return NULL;
}
