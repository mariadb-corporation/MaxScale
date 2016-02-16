/**
 * @file config_check.cpp Simple configuration checking class
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

bool test_config_works(const char* config, const char **expected_output = NULL)
{
    const char *argv[] =
        {
            config,
            "--quiet"
            NULL
        };

    TestConnections * Test = new TestConnections(1, (char**)argv);
    Test->set_timeout(10);
    if (expected_output)
    {
        Test->check_log_err((char *) "Unsupported router option \"slave\"", TRUE);
        Test->check_log_err((char *) "Couldn't find suitable Master", FALSE);
    }
    Test->check_maxscale_alive();
    Test->copy_all_logs();
    return Test->global_result == 0;
}
