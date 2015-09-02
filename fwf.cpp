/**
 * @file fwf - Firewall filtyer test
 * - setup Firewall filter to use rules from rule file fw/ruleXX, where XX - number of sub-test
 * - execute queries for fw/passXX file, expect OK
 * - execute queries from fw/denyXX, expect Access Denied error (mysql_error 1141)
 * - repeat for all XX
 * - setup Firewall filter to block queries next 2 minutes using 'at_time' statement (see template fw/rules_at_time)
 * - start sending queries, expect Access Denied now and OK after two mintes
 * - setup Firewall filter to limit a number of queries during certain time
 * - start sending queries as fast as possible, expect OK for N first quries and Access Denied for next queries
 * - wait, start sending queries again, but only one query per second, expect OK
 * - try to load rules with syntax error, expect failure for all sessions and queries
 */

#include <my_config.h>
#include <iostream>
#include <ctime>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

void copy_rules(TestConnections* Test, char * rules_name)
{
    char str[4096];
    sprintf(str, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s '%s rm -rf %s/rules; mkdir %s/rules'", Test->maxscale_sshkey, Test->access_user, Test->maxscale_IP, Test->access_sudo, Test->access_homedir,  Test->access_homedir);
    printf("Creating rules dir: %s\n", str); fflush(stdout);
    system(str);

    sprintf(str, "scp -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s/fw/%s %s@%s:%s/rules/rules.txt", Test->maxscale_sshkey, Test->test_dir, rules_name, Test->access_user, Test->maxscale_IP, Test->access_homedir);
    printf("Copying rules to Maxscale machine: %s\n", str); fflush(stdout);
    system(str);

    sprintf(str, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s '%s chown maxscale:maxscale %s/rules -R'", Test->maxscale_sshkey, Test->access_user, Test->maxscale_IP, Test->access_sudo, Test->access_homedir);
    printf("Copying rules to Maxscale machine: %s\n", str); fflush(stdout);
    system(str);
}

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

        sprintf(str, "rules%d", i);
        copy_rules(Test, str);

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
    copy_rules(Test, (char *) "rules_at_time");

/*    char time_str[100];
    char time_str1[100];
    time_t curr_time = time(NULL);
    time_t end_time = curr_time + 120;

    // current time and 'current time + 2 minutes': block delete quries for 2 minutes
    struct tm * timeinfo1 = localtime (&curr_time);


    sprintf(time_str1, "%02d:%02d:%02d", timeinfo1->tm_hour, timeinfo1->tm_min, timeinfo1->tm_sec);

    struct tm * timeinfo2 = localtime (&end_time);
    sprintf(time_str, "%s-%02d:%02d:%02d", time_str1, timeinfo2->tm_hour, timeinfo2->tm_min, timeinfo2->tm_sec);*/

    sprintf(str, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'start_time=`date +%%T`; stop_time=` date --date \"now +2 mins\" +%%T`; %s sed -i \"s/###time###/$start_time-$stop_time/\" %s/rules/rules.txt'", Test->maxscale_sshkey, Test->access_user, Test->maxscale_IP, Test->access_sudo, Test->access_homedir);
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
    printf("Copying rules to Maxscale machine: %s\n", str); fflush(stdout);
    copy_rules(Test, (char *) "rules_limit_queries");

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
        gettimeofday(&t2, NULL);
        elapsedTime = (t2.tv_sec - t1.tv_sec);
        elapsedTime += (double) (t2.tv_usec - t1.tv_usec) / 1000000.0;
    } while ((execute_query(Test->conn_rwsplit, "SELECT * FROM t1") != 0) && (elapsedTime < 10));

    printf("Quries were blocked during %f (using clock_gettime())\n", elapsedTime);
    printf("Quries were blocked during %lu (using time())\n", time(NULL)-start_time_clock);
    if ((elapsedTime > 6) or (elapsedTime < 4)) {
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
    Test->stop_maxscale();

    printf("Trying rules with syntax error\n");
    printf("Copying rules to Maxscale machine: %s\n", str); fflush(stdout);
    copy_rules(Test, (char *) "rules_syntax_error");

    Test->start_maxscale();
    Test->connect_rwsplit();

    if (execute_query(Test->conn_rwsplit, "SELECT * FROM t1") == 0) {
        global_result++;
        printf("Rule has syntax error, but query OK");
    }

    Test->copy_all_logs(); return(global_result);
}


