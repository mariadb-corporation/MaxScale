/**
 * @file bug493.cpp regression case for bug 493 ( Can have same section name multiple times without warning)
 *
 * - Maxscale.cnf in which 'server2' is difined twice and tets checks error log for proper error message.
 * - check if Maxscale is alive
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = CheckLogErr((char *) "Error : Configuration object 'server2' has multiple parameters names", TRUE);
    global_result += CheckMaxscaleAlive();
    Test->Copy_all_logs(); return(global_result);
}
