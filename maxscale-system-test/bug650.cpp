/**
 * @file bug650.cpp  regression case for bug 650 ("Hints, RWSplit: MaxScale goes into infinite loop and crashes") and bug645
 * - try simple query using all services
 * - check for errors in the log
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);
    test.check_maxscale_alive();
    test.check_log_err( "Couldn't find suitable Master from 2 candidates", true);
    test.check_log_err( "Failed to create new router session for service 'RW_Split'", true);
    return test.global_result;
}
