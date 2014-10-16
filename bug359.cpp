#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    int global_result = CheckLogErr((char *) "Warning : Unsupported router option \"slave\"", TRUE);
    global_result    += CheckLogErr((char *) "Error : Couldn't find suitable Master", FALSE);
    return(global_result);
}
