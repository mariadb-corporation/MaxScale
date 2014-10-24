#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    int global_result = CheckLogErr((char *) "Unable to find filter 'testfilter'", TRUE);
    global_result += CheckMaxscaleAlive();
    return(global_result);
}

