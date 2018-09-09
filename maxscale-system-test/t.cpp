#include <iostream>
#include <ctime>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int main(int argc, char* argv[])
{
    char time_str[100];
    time_t curr_time = time(NULL);
    time_t end_time = curr_time + 120;

    printf("%lu %lu\n", curr_time, end_time);

    // current time and 'current time + 2 minutes': block delete quries for 2 minutes
    struct tm* timeinfo1 = localtime (&curr_time);


    printf("%02d:%02d:%02d\n", timeinfo1->tm_hour, timeinfo1->tm_min, timeinfo1->tm_sec);
    struct tm* timeinfo2 = localtime (&end_time);
    printf("%02d:%02d:%02d\n", timeinfo2->tm_hour, timeinfo2->tm_min, timeinfo2->tm_sec);

    sprintf(time_str,
            "%02d:%02d:%02d-%02d:%02d:%02d",
            timeinfo1->tm_hour,
            timeinfo1->tm_min,
            timeinfo1->tm_sec,
            timeinfo2->tm_hour,
            timeinfo2->tm_min,
            timeinfo2->tm_sec);

    printf("%s\n", time_str);
}
