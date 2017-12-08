/**
 * @file bug643.cpp  regression case for bugs 645 ("Tee filter with readwritesplit service hangs MaxScale")
 *
 * - Try to connect to all services except 4016
 * - Try simple query on all services
 * - Check log for presence of "Couldn't find suitable Master from 2 candidates" errors
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);
    test.check_maxscale_alive(0);
    test.check_log_err(0,  "Couldn't find suitable Master from 2 candidates", true);
    return test.global_result;
}
