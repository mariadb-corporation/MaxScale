/**
 * @file mxs657_restart_service.cpp mxs657 regression case ("Debug assertion when service is shut down and
 * restarted repeatedly")
 *
 * - shutdown and restart RW Split Router in the loop from a number of threads
 * Note: does not work crash reliable way with 'smoke' option
 */



#include <iostream>
#include <unistd.h>
#include <maxtest/testconnections.hh>

using namespace std;
void* query_thread1(void* ptr);
TestConnections* Test;
bool exit_flag = false;
const char* shutdown_cmd;
const char* restart_cmd;

const char* router_sht = "stop service RW-Split-Router";
const char* router_rst = "start service RW-Split-Router";

const char* listener_sht = "stop service RW-Split-Listener";
const char* listener_rst = "start service RW-Split-Listener";

const char* monitor_sht = "stop service MySQL-Monitor";
const char* monitor_rst = "start service MySQL-Monitor";

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

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}

void* query_thread1(void* ptr)
{
    while (!exit_flag)
    {
        Test->maxctrl(shutdown_cmd);
        Test->maxctrl(restart_cmd);
    }

    return NULL;
}
