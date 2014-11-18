/**
 * @file bug495.cpp regression case for bug 495 ( Referring to a nonexisting server in servers=... doesn't even raise a warning )
 *
 * - Maxscale.cnf with "servers= server1, server2,server3  ,server4,server5"
 * but 'server5' is not defined. Test checks error log for proper error message.
 * - check if Maxscale is alive
 */

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    int global_result = CheckLogErr((char *) "Error: Unable to find server", TRUE);
    global_result += CheckMaxscaleAlive();
    return(global_result);
}
