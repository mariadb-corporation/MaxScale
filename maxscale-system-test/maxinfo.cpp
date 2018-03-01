/**
 * @file maxinfo.cpp maxinfo JSON listener test
 * - sends 1000 'status' request to the listener
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

#include <stdio.h>
#include <string.h>
#include "maxinfo_func.h"

using namespace std;

//const int N = 9;
//const char * resources[N] = {"variables",  "status", "services",  "listeners", "modules", "sessions", "clients", "servers", "eventTimes"};
const int N = 8;
const char * resources[N] = {"variables",  "status", "services",  "listeners", "modules", "sessions", "clients", "servers"};

bool exit_flag = false;

void *maxinfo_thread( void *ptr );
int threads_num = 25;
TestConnections * Test;

int main(int argc, char *argv[])
{
    Test = new TestConnections(argc, argv);
    int sleep_time = Test->smoke ? 30 : 1000;

    Test->set_timeout(sleep_time + 100);

    pthread_t thread1[threads_num];
    int  iret1[threads_num];

    int i;
    for (i = 0; i < threads_num; i++)
    {
        iret1[i] = pthread_create(&thread1[i], NULL, maxinfo_thread, NULL);
    }

    sleep(sleep_time);

    exit_flag = true;
    Test->set_timeout(120);
    for (i = 0; i < threads_num; i++)
    {
        pthread_join(thread1[i], NULL);
    }

    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}

void *maxinfo_thread( void *ptr )
{
    char * result;
    int ind;

    while (! exit_flag)
    {
        ind = rand() % N;
        result = get_maxinfo(resources[ind], Test);
        if (result != NULL)
        {
            Test->tprintf("Query %s, result: \n%s\n", resources[ind], result);
            free(result);
        }
        else
        {
            Test->add_result(1, "Can't get result from maxinfo, query %s\n", resources[ind]);
        }
    }

}
