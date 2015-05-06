/**
 * @file mxs118.cpp bug mxs118 regression case ("Two monitors loaded at the same time result into not working installation")
 *
 * - Configure two monitors using same backend serves
 * - try to connect to maxscale
 * - check logs for warning
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->connect_maxscale();

    int global_result = check_log_err((char *) "Multiple monitors are monitoring server", TRUE);


    Test->copy_all_logs(); return(global_result);
}

