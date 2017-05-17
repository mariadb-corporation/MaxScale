/**
 * @file Simple dummy configuration program for non-C++ tests
 * - Configure Maxscale (prepare maxscale.cnf and copy it to Maxscale machine)
 * - check backends
 * - try to restore broken backends
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        return 1;
    }

    int local_argc = argc - 1;
    char** local_argv = &argv[1];

    TestConnections * Test = new TestConnections(local_argc, local_argv);

    sleep(3);

    return 0;
}
