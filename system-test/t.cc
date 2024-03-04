/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <ctime>
#include <maxtest/testconnections.hh>
#include <maxtest/sql_t1.h>

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
