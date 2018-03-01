#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "execute_cmd.h"

using namespace std;


int execute_cmd(char * cmd, char ** res)
{
    char * result;
    FILE *output = popen(cmd, "r");
    if (output == NULL)
    {
        printf("Error opening ssh %s\n", strerror(errno));
        return -1;
    }
    char buffer[10240];
    size_t rsize = sizeof(buffer);
    result =  (char*)calloc(rsize, sizeof(char));

    while (fgets(buffer, sizeof(buffer), output))
    {
        result = (char*)realloc(result, sizeof(buffer) + rsize);
        rsize += sizeof(buffer);
        strcat(result, buffer);
    }

    * res = result;

    int return_code = pclose(output);
    if (WIFEXITED(return_code))
    {
        return WEXITSTATUS(return_code);
    }
    else
    {
        return -1;
    }
}


