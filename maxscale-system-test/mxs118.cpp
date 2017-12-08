/**
 * @file mxs118.cpp bug mxs118 regression case ("Two monitors loaded at the same time result into not working installation")
 *
 * - Configure two monitors using same backend serves
 * - try to connect to maxscale
 * - check logs for warning
 */

#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    Test->maxscales->connect_maxscale(0);

    Test->check_log_err(0, (char *) "Multiple monitors are monitoring server", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

