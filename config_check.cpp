/**
 * @file config_check.cpp Simple configuration checking
 */


#include <my_config.h>
#include <stdlib.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include <sys/types.h>
#include <sys/wait.h>

using namespace std;

/**
* @brief Check if MaxScale starts with the provided configuration template
*
* @param config Configuration template name
* @param expected_output If the parameter is not NULL, it is expected to contain
* an array of null-terminated strings with the last element set to NULL
* @return True if MaxScale is running and all strings in @p expected_output are
* found from the log otherwise false
*/
bool test_config_works(const char* config, const char **expected_output = NULL)
{
    const char *argv[] =
        {
            config,
            NULL
        };
    pid_t pid = fork();

    if (pid == 0)
    {
        TestConnections * Test = new TestConnections(1, (char**)argv);
        Test->stop_timeout();
        for (int i = 0; expected_output && expected_output[i]; i++)
        {
            Test->check_log_err((char *)expected_output[i], TRUE);
        }
        sleep(5);
        Test->check_maxscale_processes(1);
        Test->stop_maxscale();
        sleep(5);
        Test->check_maxscale_processes(0);
        exit(Test->global_result);
        return false;
    }
    else if (pid > 0)
    {
        int rc = 0;
        wait(&rc);
        printf("Process exited with status %d\n", WIFEXITED(rc) ? WEXITSTATUS(rc) : -1);
        return WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
    }
    else
    {
        return false;
    }
}
