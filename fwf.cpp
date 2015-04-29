/**
 * @file fwf
 */

#include <my_config.h>
#include <iostream>
#include <ctime>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int local_result;
    char str[4096];
    char sql[4096];
    char pass_file[4096];
    char deny_file[4096];
    FILE* file;

    Test->read_env();
    Test->print_env();

    int N = 9;
    int i;

    for (i = 1; i < N+1; i++){
        local_result = 0;

        Test->stop_maxscale();
        sprintf(str, "scp -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s/fw/rules%d root@%s:/home/ec2-user/rules.txt", Test->maxscale_sshkey, Test->test_dir, i, Test->maxscale_IP);
        printf("Copying rules to Maxscale machine: %s\n", str); fflush(stdout);
        system(str);
        Test->start_maxscale();
        Test->connect_rwsplit();

        sprintf(pass_file, "%s/fw/pass%d", Test->test_dir, i);
        sprintf(deny_file, "%s/fw/deny%d", Test->test_dir, i);

        file = fopen(pass_file, "r");
        if (file != NULL) {
            printf("********** Trying queries that should be OK ********** \n");fflush(stdout);
            while (fgets(sql, sizeof(sql), file)) {
                if (strlen(sql) > 1) {
                    printf("%s", sql);fflush(stdout);
                    local_result += execute_query(Test->conn_rwsplit, sql);
                }
            }
            fclose(file);
        } else {
            printf("Error opening query file\n");
            global_result++;
        }

        file = fopen(deny_file, "r");
        if (file != NULL) {
            printf("********** Trying queries that should FAIL ********** \n");fflush(stdout);
            while (fgets(sql, sizeof(sql), file)) {
                if (strlen(sql) > 1) {
                    printf("%s", sql);fflush(stdout);
                    execute_query(Test->conn_rwsplit, sql);
                    if (mysql_errno(Test->conn_rwsplit) != 1141) {
                        printf("Query succeded, but fail expected, errono is %d\n", mysql_errno(Test->conn_rwsplit));fflush(stdout);
                        local_result++;
                    }
                }
            }
            fclose(file);
        } else {
            printf("Error opening query file\n");
            global_result++;
        }
        global_result += local_result;
        if (local_result == 0) {
            printf("********** rules%d test PASSED\n", i);fflush(stdout);
        } else {
            printf("********** rules%d test FAILED\n", i);fflush(stdout);
        }

        mysql_close(Test->conn_rwsplit);
    }

    Test->stop_maxscale();

    // Test for at_times clause
    printf("Trying at_times clause\n");

    sprintf(str, "scp -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s/fw/rules_at_time root@%s:/home/ec2-user/rules.txt", Test->maxscale_sshkey, Test->test_dir, Test->maxscale_IP);
    printf("Copying rules to Maxscale machine: %s\n", str); fflush(stdout);
    system(str);

/*    char time_str[100];
    char time_str1[100];
    time_t curr_time = time(NULL);
    time_t end_time = curr_time + 120;

    // current time and 'current time + 2 minutes': block delete quries for 2 minutes
    struct tm * timeinfo1 = localtime (&curr_time);


    sprintf(time_str1, "%02d:%02d:%02d", timeinfo1->tm_hour, timeinfo1->tm_min, timeinfo1->tm_sec);

    struct tm * timeinfo2 = localtime (&end_time);
    sprintf(time_str, "%s-%02d:%02d:%02d", time_str1, timeinfo2->tm_hour, timeinfo2->tm_min, timeinfo2->tm_sec);*/

    sprintf(str, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'start_time=`date +%%T`; stop_time=` date --date \"now +2 mins\" +%%T`; sed -i \"s/###time###/$start_time-$stop_time/\" /home/ec2-user/rules.txt'", Test->maxscale_sshkey, Test->maxscale_IP);
    printf("DELETE quries without WHERE clause will be blocked during next 2 minutes\n");
    printf("Put time to rules.txt: %s\n", str); fflush(stdout);
    system(str);

    Test->start_maxscale();
    Test->connect_rwsplit();

    printf("Trying 'DELETE FROM t1' and expecting FAILURE\n");
    execute_query(Test->conn_rwsplit, "DELETE FROM t1");
    if (mysql_errno(Test->conn_rwsplit) != 1141) {
        printf("Query succeded, but fail expected, errono is %d\n", mysql_errno(Test->conn_rwsplit));fflush(stdout);
        global_result++;
    }
    printf("Waiting 3 minutes and trying 'DELETE FROM t1', expecting OK\n");
    sleep(180);
    global_result += execute_query(Test->conn_rwsplit, "DELETE FROM t1");

    mysql_close(Test->conn_rwsplit);
    Test->stop_maxscale();

    printf("Trying limit_queries clause\n");

    sprintf(str, "scp -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s/fw/rules_limit_queries root@%s:/home/ec2-user/rules.txt", Test->maxscale_sshkey, Test->test_dir, Test->maxscale_IP);
    printf("Copying rules to Maxscale machine: %s\n", str); fflush(stdout);
    system(str);

    Test->start_maxscale();
    Test->connect_rwsplit();

    printf("Trying 10 quries as fast as possible\n");
    for (i = 0; i < 10; i++) {
        global_result += execute_query(Test->conn_rwsplit, "SELECT * FROM t1");
    }

    printf("Expecting failures during next 5 seconds\n");

    time_t start_time_clock = time(NULL);
    timeval t1, t2;
    double elapsedTime;
    gettimeofday(&t1, NULL);

    do {
    } while (execute_query(Test->conn_rwsplit, "SELECT * FROM t1") != 0);

    gettimeofday(&t2, NULL);

    elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000.0;
    elapsedTime += (double) (t2.tv_usec - t1.tv_usec) / 1000.0;

    printf("Quries were blocked during %f (using clock_gettime())\n", elapsedTime);
    printf("Quries were blocked during %lu (using time())\n", time(NULL)-start_time_clock);
    if ((elapsedTime > 5.2) or (elapsedTime < 4.8)) {
        printf("Queries were blocked during wrong time\n");
        global_result++;
    }

    printf("Trying 20 quries, 1 query / second\n");
    for (i = 0; i < 20; i++) {
        sleep(1);
        global_result += execute_query(Test->conn_rwsplit, "SELECT * FROM t1");
        printf("%d ", i);
    }
    printf("\n");

    Test->copy_all_logs(); return(global_result);
}


