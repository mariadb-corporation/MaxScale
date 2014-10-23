#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    int global_result = CheckLogErr((char *) "Unable to find filter 'non existing filter", TRUE);
    global_result = CheckLogErr((char *) ", не существуюший фильтер", TRUE);
    global_result += CheckMaxscaleAlive();
    return(global_result);
}

