
#include <my_config.h>
#include <iostream>
#include <ctime>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int main(int argc, char *argv[])
{
    time_t curr_time = time(NULL);

    struct tm * timeinfo = localtime (&curr_time);


printf("%2d:%2d:%2d\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

}
