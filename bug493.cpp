#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    int global_result = CheckLogErr((char *) "Error : Configuration object 'server2' has multiple parameters names", TRUE);
    global_result += CheckMaxscaleAlive();
    return(global_result);
}
