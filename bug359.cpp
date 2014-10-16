#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    int global_result = CheckLogErr((char *) "Warning : Unsupported router option \"slave\"");
    global_result    += CheckLogErr((char *) "Error : Couldn't find suitable Master");
    return(global_result);
}
