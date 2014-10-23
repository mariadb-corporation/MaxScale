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
