/**
 * @file bug359.cpp bug359 regression case (router_options in readwritesplit causes errors in error log)
 *
 * - Maxscale.cnf contains RWSplit router definition with raouter_option=slave.
 * - warning is expected in the log, but not an error. All Maxscale services should be alive.
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    int global_result = CheckLogErr((char *) "Warning : Unsupported router option \"slave\"", TRUE);
    global_result    += CheckLogErr((char *) "Error : Couldn't find suitable Master", FALSE);
    global_result += CheckMaxscaleAlive();
    return(global_result);
}
