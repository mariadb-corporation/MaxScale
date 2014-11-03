#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    int global_result = CheckLogErr((char *) "Error : Unable to find library for module: foobar", TRUE);
    global_result += CheckLogErr((char *) "Failed to create filter 'testfilter' for service", TRUE);
    global_result += CheckLogErr((char *) "Error : Failed to create RW Split Router session", TRUE);
    global_result += CheckMaxscaleAlive();
    return(global_result);
}

