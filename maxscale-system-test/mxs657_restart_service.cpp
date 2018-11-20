/**
 * @file mxs657_restart_service.cpp mxs657 regression case ("Debug assertion when service is shut down and
 * restarted repeatedly")
 *
 * - shutdown and restart RW Split Router in the loop from a number of threads
 * Note: does not work crash reliable way with 'smoke' option
 */



#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;
void* query_thread1(void* ptr);
TestConnections* Test;
bool exit_flag = false;
char* shutdown_cmd;
char* restart_cmd;

char* router_sht = (char*) "shutdown service \"RW Split Router\"";
char* router_rst = (char*) "restart service \"RW Split Router\"";

char* listener_sht = (char*) "shutdown service \"RW Split Listener\"";
char* listener_rst = (char*) "restart service \"RW Split Listener\"";

char* monitor_sht = (char*) "shutdown service \"MySQL Monitor\"";
char* monitor_rst = (char*) "restart service \"MySQL Monitor\"";

void sht_rst_service()
{
    int threads_num = 5;
    pthread_t thread1[threads_num];

    int i;

    for (i = 0; i < threads_num; i++)
    {
        pthread_create(&thread1[i], NULL, query_thread1, NULL);
    }

    Test->tprintf("Trying to shutdown and restart RW Split router in the loop\n");

    sleep(10);

    Test->tprintf("Done, exiting threads\n\n");

    exit_flag = true;
    for (int i = 0; i < threads_num; i++)
    {
        pthread_join(thread1[i], NULL);
    }

    Test->tprintf("Done!\n");
}

int main(int argc, char* argv[])
{
    Test = new TestConnections(argc, argv);

    Test->tprintf("Shutdown and restart Router\n");

    shutdown_cmd = router_sht;
    restart_cmd = router_rst;

    sht_rst_service();

    Test->tprintf("Shutdown and restart Listener\n");

    shutdown_cmd = listener_sht;
    restart_cmd = listener_rst;

    sht_rst_service();

    Test->tprintf("Shutdown and restart Monitor\n");

    shutdown_cmd = monitor_sht;
    restart_cmd = monitor_rst;

    sht_rst_service();

    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}

void* query_thread1(void* ptr)
{
    while (!exit_flag)
    {
        Test->maxscales->execute_maxadmin_command(0, shutdown_cmd);
        Test->maxscales->execute_maxadmin_command(0, restart_cmd);
    }

    return NULL;
}
