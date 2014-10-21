#include <my_config.h>
#include "testconnections.h"
#include "sysbench_commands.h"

TestConnections * Test ;

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;
void *kill_vm_thread( void *ptr );

int main()
{
    Test = new TestConnections();
    int global_result = 0;
    pthread_t kill_vm_thread1;
    int check_iret;
    char sys1[4096];

    Test->ReadEnv();
    Test->PrintIP();

    printf("Connecting to RWSplit %s\n", Test->Maxscale_IP);
    Test->ConnectRWSplit();

    sprintf(&sys1[0], sysbench_prepare, Test->Maxscale_IP);
    printf("Preparing sysbench tables\n%s\n", sys1);
    fflush(stdout);
    if (system(sys1) != 0) {
        printf("Error executing sysbench prepare\n");
        exit(1);
    }

    check_iret = pthread_create( &kill_vm_thread1, NULL, kill_vm_thread, NULL);
    pthread_join(kill_vm_thread1, NULL);

    sprintf(&sys1[0], sysbench_command, Test->Maxscale_IP);
    printf("Executing sysbench tables\n%s\n", sys1);
    fflush(stdout);
    if (system(sys1) != 0) {
        printf("Error executing sysbench test\n");
        exit(1);
    }

    //global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest1");
    //global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest2");
    //global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest3");
    //global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest4");

    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE sbtest");

    Test->CloseRWSplit();

    exit(global_result);
}


void *kill_vm_thread( void *ptr )
{
    unsigned int old_slave;
    int global_result = 0;

    sleep(20);
    printf("Checking current slave\n");
    old_slave = FindConnectedSlave(Test, &global_result);

    char sys1[4096];
    printf("Killing VM %s\n", Test->repl->IP[old_slave]);
    fflush(stdout);
    sprintf(&sys1[0], "%s %s", Test->KillVMCommand, Test->repl->IP[old_slave]);
    system(sys1);
    return NULL;
}
