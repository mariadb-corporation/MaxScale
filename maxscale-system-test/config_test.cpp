/**
* Simple configuration test
*/

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "config_check.h"

const char *config[] =
{
    "galera_priority",
    "ssl",
    "regexfilter1",
    NULL
};

int main()
{
    int rval = 0;

    for (int i = 0; config[i]; i++)
    {
        printf("Testing %s...\n", config[i]);
        if (test_config_works(config[i], NULL))
        {
            printf("SUCCESS\n");
        }
        else
        {
            printf("FAILED\n");
            rval++;
        }
    }

    return rval;
}
